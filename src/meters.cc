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

#include "internal.h"

#include <fcntl.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define METER_NAME "com.couchbase.client.c"

const char *op_name_from_store_operation(lcb_STORE_OPERATION operation)
{
    switch (operation) {
        case LCB_STORE_INSERT:
            return "insert";
        case LCB_STORE_REPLACE:
            return "replace";
        case LCB_STORE_APPEND:
            return "append";
        case LCB_STORE_PREPEND:
            return "prepend";
        case LCB_STORE_UPSERT:
            return "upsert";
        default:
            return "unknown";
    }
}

std::vector<lcb::metrics::tag> create_tags(const char *op, const char *svc)
{
    std::vector<lcb::metrics::tag> retval;
    if (nullptr != svc) {
        retval.push_back({"db.couchbase.service", svc});
        if (nullptr != op) {
            retval.push_back({"db.operation", op});
        }
    }
    return retval;
}

void record_op_latency(const char *op, const char *svc, lcb_INSTANCE *instance, mc_PACKET *request)
{
    if (LCBT_SETTING(instance, op_metrics_enabled) && instance->op_metrics) {
        instance->op_metrics->valueRecorder(op, create_tags(op, svc))
            .recordValue(gethrtime() - (MCREQ_PKT_RDATA(request)->start));
    }
}

void record_kv_op_latency_store(lcb_INSTANCE *instance, mc_PACKET *request, lcb_RESPSTORE *response)
{
    record_kv_op_latency(op_name_from_store_operation(response->op), instance, request);
}

void record_kv_op_latency(const char *op, lcb_INSTANCE *instance, mc_PACKET *request)
{
    record_op_latency(op, "kv", instance, request);
}

void record_http_op_latency(const char *op, const char *svc, lcb_INSTANCE *instance, hrtime_t start)
{
    if (nullptr != svc && LCBT_SETTING(instance, op_metrics_enabled) && instance->op_metrics) {
        instance->op_metrics->valueRecorder(svc, create_tags(op, svc)).recordValue(gethrtime() - start);
    }
}

using namespace lcb::metrics;

CustomValueRecorder::CustomValueRecorder(lcbmetrics_RECORDER *recorder) : recorder_(recorder) {}

CustomValueRecorder::~CustomValueRecorder()
{
    // delete the recorder here.
    delete recorder_;
}

void CustomValueRecorder::recordValue(lcb_U64 value)
{
    recorder_->record_value(recorder_, value);
}

CustomMeter::CustomMeter(const lcbmetrics_METER *meter) : meter_(meter)
{
    // perhaps an assert on null?
}

ValueRecorder &CustomMeter::valueRecorder(const std::string &name, const std::vector<tag> &tags)
{
    auto it = valueRecorders_.find(name);
    if (it != valueRecorders_.end()) {
        return it->second;
    }

    // lets add a new one
    std::vector<lcbmetrics_TAG> callback_tags;
    for (auto &t : tags) {
        callback_tags.push_back({t.key.c_str(), t.value.c_str()});
    }
    lcbmetrics_RECORDER *recorder = meter_->new_recorder(meter_, name.c_str(), &callback_tags.front(), tags.size());

    auto ins = valueRecorders_.emplace(name, recorder);
    return ins.first->second;
}

AggregatingValueRecorder::AggregatingValueRecorder(const std::string &name, const std::vector<tag> &tags)
    : name_(name), tags_(tags), histogram_(lcb_histogram_create())
{
}

AggregatingValueRecorder::~AggregatingValueRecorder()
{
    lcb_histogram_destroy(histogram_);
}

void AggregatingValueRecorder::recordValue(lcb_U64 value)
{
    lcb_histogram_record(histogram_, value);
}

const std::string &AggregatingValueRecorder::name() const
{
    return name_;
}
const std::vector<tag> &AggregatingValueRecorder::tags() const
{
    return tags_;
}
void AggregatingValueRecorder::flush(FILE *stream)
{
    fprintf(stream, "%s, tags: {", METER_NAME);
    for (auto &t : tags_) {
        fprintf(stream, " %s=%s ", t.key.c_str(), t.value.c_str());
    }
    fprintf(stream, "}\n");
    lcb_histogram_print(histogram_, stream);
    lcb_histogram_destroy(histogram_);
    histogram_ = lcb_histogram_create();
    fflush(stream);
}

AggregatingMeter::AggregatingMeter(lcb_INSTANCE *lcb) : lcb_(lcb), timer_(lcb_->iotable, this)
{
    timer_.rearm(lcb_->settings->op_metrics_flush_interval);
}

ValueRecorder &AggregatingMeter::valueRecorder(const std::string &name, const std::vector<tag> &tags)
{
    auto it = std::find_if(valueRecorders_.begin(), valueRecorders_.end(),
                           [&](AggregatingValueRecorder &r) { return name == r.name(); });
    if (it != valueRecorders_.end()) {
        return *it;
    }
    // lets add a new one
    valueRecorders_.emplace_back(name, tags);
    return valueRecorders_.back();
}

void AggregatingMeter::flush()
{
    timer_.rearm(lcb_->settings->op_metrics_flush_interval);
    char buffer[2048];
    for (auto &r : valueRecorders_) {
        // To work with the logger, we need to capture the output by
        // writing to a pipe, then reading into the logger.
        size_t sz = 0;
        int temp_pipe[2];
#ifdef _WIN32
        HANDLE handles[2];
        DWORD pipe_state = PIPE_NOWAIT;
        if (CreatePipe(&handles[0], &handles[1], NULL, 0) &&
            SetNamedPipeHandleState(handles[0], &pipe_state, NULL, NULL)) {
            // convert windows handles to fds
            temp_pipe[0] = _open_osfhandle((intptr_t)handles[0], _O_TEXT);
            temp_pipe[1] = _open_osfhandle((intptr_t)handles[1], _O_TEXT);
#else
        if (0 == pipe(temp_pipe) && -1 != fcntl(temp_pipe[0], F_SETFL, O_NONBLOCK)) {
#endif

            // open file* for each
            FILE *out = fdopen(temp_pipe[0], "r");
            FILE *in = fdopen(temp_pipe[1], "w");

            r.flush(in);

            // now read entire pipe and log
            std::string str_buffer;
            while ((sz = fread(&buffer, 1, sizeof(buffer), out)) > 0) {
                str_buffer.append(buffer, sz);
            }
            // cleanup
            fclose(out);
            fclose(in);

            // log
            lcb_log(lcb_->settings, "op_metrics", LCB_LOG_INFO, __FILE__, __LINE__, "%s", str_buffer.c_str());
        }
    }
}
