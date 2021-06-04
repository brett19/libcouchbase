/*
 *     Copyright 2021 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/**
 *  for default metrics:  LCB_LOGLEVEL=2 ./otel_metrics <anything>
 *  for otel metrics ./otel_metrics
 */

#include <atomic>
#include <csignal>
#include <iostream>

#include <libcouchbase/couchbase.h>
#include <libcouchbase/utils.h>
#include <opentelemetry/exporters/ostream/metrics_exporter.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/controller.h>
#include <opentelemetry/sdk/metrics/meter.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/ungrouped_processor.h>
#include <opentelemetry/metrics/sync_instruments.h>

namespace sdkmetrics = opentelemetry::sdk::metrics;
namespace apimetrics = opentelemetry::metrics;
namespace nostd = opentelemetry::nostd;

std::atomic<bool> running{true};

// Initialize and set the global MeterProvider
auto provider = nostd::shared_ptr<metrics_api::MeterProvider>(new sdkmetrics::MeterProvider);

// Create a new Meter from the MeterProvider
nostd::shared_ptr<metrics_api::Meter> meter = provider->GetMeter("Test", "0.1.0");

// Create the controller with Stateless Metrics Processor
// NOTE: the default meter has limited aggregator choices. No histograms, which
// would be nice for log output.
sdkmetrics::PushController controller(
    meter, std::unique_ptr<sdkmetrics::MetricsExporter>(new opentelemetry::exporter::metrics::OStreamMetricsExporter),
    std::shared_ptr<sdkmetrics::MetricsProcessor>(new opentelemetry::sdk::metrics::UngroupedMetricsProcessor(true)), 5);

nostd::shared_ptr<metrics_api::ValueRecorder<int>> counter;

void signal_handler(int signal)
{
    running = false;
}

static void check(const char *msg, lcb_STATUS err)
{
    if (err != LCB_SUCCESS) {
        std::cerr << msg << ". Error " << lcb_strerror_short(err) << std::endl;
        return exit(1);
    }
}

static void store_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPSTORE *resp)
{
    check(lcb_strcbtype(cbtype), lcb_respstore_status(resp));
}

static void get_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPGET *resp)
{
    check(lcb_strcbtype(cbtype), lcb_respget_status(resp));
}

static void row_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPQUERY *resp)
{
    check(lcb_strcbtype(cbtype), lcb_respquery_status(resp));
}

static void open_callback(lcb_INSTANCE *, lcb_STATUS rc)
{
    check("open bucket", rc);
}

struct otel_recorder {
    nostd::shared_ptr<apimetrics::BoundValueRecorder<int>> recorder;
};

void record_callback(void *cookie, uint64_t val)
{
    // the value is the latency, in ns.  Lets report in us throughout.
    static_cast<otel_recorder *>(cookie)->recorder->record(val / 1000);
}

static lcbmetrics_RECORDER *new_recorder(const char *name, const lcbmetrics_TAG *tags, size_t num_tags)
{
    // convert the tags array to a map for the KeyValueIterableView
    std::map<std::string, std::string> keys;
    for (int i = 0; i < num_tags; i++) {
        keys[tags[i].key] = tags[i].value;
    }
    otel_recorder *ot = new otel_recorder();
    if (!counter) {
        counter = meter->NewIntValueRecorder(name, "oltp_metrics example", "us", true);
    }
    ot->recorder = counter->bindValueRecorder(opentelemetry::common::KeyValueIterableView<decltype(keys)>{keys});
    lcbmetrics_RECORDER *recorder;
    lcbmetrics_create_value_recorder(&recorder);
    lcbmetrics_recorder_set_cookie(recorder, static_cast<void *>(ot));
    lcbmetrics_recorder_set_record_value_callback(recorder, record_callback);
    return recorder;
}

int main(int argc, char *argv[])
{
    lcb_STATUS err;
    lcb_CREATEOPTS *options;
    lcb_CMDSTORE *scmd;
    lcb_CMDGET *gcmd;
    lcb_CMDQUERY *qcmd;
    lcb_INSTANCE *instance;
    lcbmetrics_METER *metrics;

    std::string connection_string = "couchbase://127.0.0.1";
    std::string username = "Administrator";
    std::string password = "password";
    std::string bucket = "default";
    std::string query = "SELECT * from `default` LIMIT 10";
    std::string opt;

    // Allow user to pass in no-otel to see default behavior
    // Ideally we will take more options, say to export somewhere other than the stderr, in the future.
    if (argc > 1) {
        opt = argv[1];
    }
    // catch sigint, and delete the external metrics...
    std::signal(SIGINT, signal_handler);

    lcb_createopts_create(&options, LCB_TYPE_CLUSTER);
    lcbmetrics_create_external_metrics(&metrics);
    lcbmetrics_set_create_value_recorder_callback(metrics, new_recorder);
    if (opt.empty()) {
        lcb_createopts_external_metrics(options, metrics);
        controller.start();
    }
    lcb_createopts_connstr(options, connection_string.data(), connection_string.size());
    lcb_createopts_credentials(options, username.data(), username.size(), password.data(), password.size());
    check("create connection handle", lcb_create(&instance, options));
    lcb_createopts_destroy(options);

    check("schedule connect", lcb_connect(instance));
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    check("cluster bootstrap", lcb_get_bootstrap_status(instance));

    lcb_set_open_callback(instance, open_callback);
    check("schedule open bucket", lcb_open(instance, bucket.data(), bucket.size()));
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    // set metrics callback if desired
    if (!opt.empty()) {
        // for default, lets set the interval low
        uint32_t interval = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(10)).count());
        lcb_cntl(instance, LCB_CNTL_SET, LCB_CNTL_OP_METRICS_FLUSH_INTERVAL, &interval);
    }

    // enable op metrics, and set flush interval to 10 sec
    int enable = 1;
    lcb_cntl(instance, LCB_CNTL_SET, LCB_CNTL_ENABLE_OP_METRICS, &enable);

    /* Assign the handlers to be called for the operation types */
    lcb_install_callback(instance, LCB_CALLBACK_GET, (lcb_RESPCALLBACK)get_callback);
    lcb_install_callback(instance, LCB_CALLBACK_STORE, (lcb_RESPCALLBACK)store_callback);
    lcb_install_callback(instance, LCB_CALLBACK_QUERY, (lcb_RESPCALLBACK)row_callback);

    // Just loop until a sigint.  We will do an upsert, then a get, then a query.
    while (running.load()) {
        // upsert an item
        lcb_cmdstore_create(&scmd, LCB_STORE_UPSERT);
        lcb_cmdstore_key(scmd, "key", strlen("key"));
        lcb_cmdstore_value(scmd, "value", strlen("value"));
        check("schedule store", lcb_store(instance, NULL, scmd));
        lcb_cmdstore_destroy(scmd);
        lcb_wait(instance, LCB_WAIT_DEFAULT);

        // fetch the item back
        lcb_cmdget_create(&gcmd);
        lcb_cmdget_key(gcmd, "key", strlen("key"));
        check("schedule get", lcb_get(instance, NULL, gcmd));
        lcb_cmdget_destroy(gcmd);
        lcb_wait(instance, LCB_WAIT_DEFAULT);

        // Now, a query
        lcb_cmdquery_create(&qcmd);
        lcb_cmdquery_statement(qcmd, query.data(), query.size());
        lcb_cmdquery_callback(qcmd, row_callback);
        check("schedule query", lcb_query(instance, NULL, qcmd));
        lcb_cmdquery_destroy(qcmd);
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }
    return 0;
}
