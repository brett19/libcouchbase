/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017-2020 Couchbase, Inc.
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

#ifndef LCB_METRICS_H
#define LCB_METRICS_H

// As part of implementation of metrics and tracing, the metrics.h file was moved to
// the iometrics.h file.  To ensure backwards compatibility, we include iometrics.h.
#include <libcouchbase/iometrics.h>

/**
 * @ingroup lcb-public-api
 * @defgroup lcb-operation-metrics Operation Metrics
 * @brief Output per-operation latencies
 *
 * libcouchbase will keep track of the latencies for each operation performed on an
 * instance.  Unlike @ref lcb_get_timings, this will aggregate the latencies
 * for each operation (get, store, etc...) separately.  The latencies are measured
 * from the time the command is called (e.g. lcb_get) until the associated callback is called.
 *
 * The default metrics provider will output a separate histogram for each
 * operation to stdout.  This happens periodically, see @ref LCB_CNTL_OP_METRICS_FLUSH_INTERVAL
 * for details on setting this.
 *
 * An external metrics collector, such as OpenTelemetry, can be used instead.  The @ref lcb_CREATEOPTS
 * accept an @ref lcbmetrics_RECORDER struct which, when provided, will allow for an external
 * library (such as OpenTelemetry) to be called.
 *
 * add-to-group lcb-operation-metrics {
 */

/**
 * @brief Encapsulates an external metrics collector.
 *
 * The default metrics collector used by libcouchbase will aggregate the metrics into a
 * histogram, and output that to the logs periodically.  However, you can use your own
 * metrics collection library instead.
 *
 * All the metrics are defined by a name, and a set of tags.  Instead of recording this
 * internally, libcouchbase can call the supplied callback instead, and the callback can
 * do what it pleases with the data.
 *
 * There are 2 callbacks needed.  One binds a recorder to a name and a set of tags.  That
 * returns a structure which has a second callback, which will be called for that metric
 * with the latency, in microseconds.
 */
typedef struct lcbmetrics_METER_ lcbmetrics_METER;

/**
 * @brief External operation metrics value recorder.
 *
 * The external metrics collector will bind a value recorder to
 * a set of tags (associated with a specific operation), and return
 * this structure to libcouchbase.
 */
typedef struct lcbmetrics_RECORDER_ lcbmetrics_RECORDER;

/**
 * @brief Operation metrics tags
 *
 * When using an external callback to collect metrics, the tags that define
 * the metric are represented by this struct.
 */
typedef struct {
    const char *key;
    const char *value;
} lcbmetrics_TAG;

/**
 *  @brief External callback to record latency for a given metric.
 *
 * The external metrics collector will implement this function to record
 * metrics.
 *
 * @param cookie pointer to the external metric recorder for a given metric.
 * @param value value of the metric.  Currently, this is the latency of an
                operation, in microseconds.
 */
typedef void (*lcbmetrics_RECORD_VALUE)(const lcbmetrics_RECORDER *recorder, uint64_t value);

/**
 * @brief External callback function to collect metrics
 *
 * The external metrics collector will implement this function to create a new recorder.
 *
 * @param name The name of the metric to be recorded
 * @param tags Pointer to an array containing a set of tags that define the metric being recorded.
 * @param num_tags Number of tags in the array.
 * @return the external metrics recorder structure to be used for the metric defined by the tags.
 */
typedef lcbmetrics_RECORDER *(*lcbmetrics_CREATE_RECORDER)(const lcbmetrics_METER *meter, const char *name,
                                                           const lcbmetrics_TAG *tags, size_t num_tags);

/**
 * @brief Allocate an external metrics structure.
 *
 * Once the external metrics collector has been associated with an instance, it will
 * be automatically deleted when the instance is destroyed via @ref lcb_destroy.
 *
 * @params pmetrics points to the allocated external meter structure.
 * @return LCB_SUCCESS if allocated successfully.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_create(lcbmetrics_METER **pmeter, void *cookie);

/**
 * @brief Set the callback for creating a new value recorder in the external meter.
 *
 * @param metrics the external metrics structure.
 * @param callback the callback which creates a new recorder.
 * @returns LCB_SUCCESS if successful.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_create_recorder_callback(lcbmetrics_METER *meter, lcbmetrics_CREATE_RECORDER callback);

/**
 * @brief Get the cookie for the external meter.
 *
 * @param meter The meter.
 * @param cookie A pointer to an external resource needed to create recorders.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_cookie(const lcbmetrics_METER *meter, void **cookie);

/**
 * @brief Deallocate external metrics collector.
 *
 * If the metrics are associated with an lcb instance, it is deallocated automatically when the
 * lcb instance is deallocated with an @ref lcb_destroy call.  This call is only necessary in
 * circumstances where that isn't possible.
 *
 * @param metrics External metrics collector to deallocate.
 * @return LCB_SUCCESS when successful.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_destroy(lcbmetrics_METER *meter);

/**
 * @brief Allocate an external metrics recorder.
 *
 * @param pointer to the allocated recorder.
 * @return LCB_SUCCESS if successful.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_create(lcbmetrics_RECORDER **recorder, void *cookie);

/**
 * @brief Set record value callback in external recorder.
 *
 * Note that the recorder is stored internally in libcouchbase, once it is returned in a new recorder callback.
 * When the external metrics is deleted in @ref lcbmetrics_destroy_external_metrics call, all recorders that
 * were returned in the callback to create a new recorder are deleted automatically.
 *
 * @param recorder The external recorder.
 * @param callback The callback to use to record the metric values.
 * @return LCB_SUCCESS if successful.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_record_value_callback(lcbmetrics_RECORDER *recorder, lcbmetrics_RECORD_VALUE callback);

/**
 * @brief Get the cookie for the external recorder.
 *
 * @param recorder The recorder.
 * @param cookie A pointer to an external resource needed to record metric values.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_cookie(const lcbmetrics_RECORDER *recorder, void **cookie);

/**
 * @brief Deallocate an external metrics recorder.
 *
 * This is not usually necessary.  Any recorder returned in a call to the new recorder
 * callback is deleted internally when the external metrics are deleted in the
 * @ref lcbmetrics_destroy_external_metrics call.  However, if a recorder was somehow
 * created but never returned in the callback, you can delete it by calling this.
 *
 * @param recorder The recorder to be deallocated.
 * @return LCB_SUCCESS when successful.
 */
LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_destroy(lcbmetrics_RECORDER *recorder);

/** @} (Group: Operation Metrics) */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif // LCB_METRICS_H
