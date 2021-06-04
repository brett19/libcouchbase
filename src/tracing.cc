/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011-2020 Couchbase, Inc.
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

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_create(lcbmetrics_METER **pmeter, void *cookie)
{
    *pmeter = new lcbmetrics_METER;
    (*pmeter)->cookie = cookie;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_create_recorder_callback(lcbmetrics_METER *meter, lcbmetrics_CREATE_RECORDER recorder)
{
    meter->new_recorder = recorder;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_cookie(lcbmetrics_METER *meter, void **cookie)
{
    *cookie = meter->cookie;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_meter_destroy(lcbmetrics_METER *pmeter)
{
    delete pmeter;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_create(lcbmetrics_RECORDER **recorder, void *cookie)
{
    *recorder = new lcbmetrics_RECORDER;
    (*recorder)->cookie = cookie;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_cookie(lcbmetrics_RECORDER *recorder, void **cookie)
{
    *cookie = recorder->cookie;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_record_value_callback(lcbmetrics_RECORDER *recorder, lcbmetrics_RECORD_VALUE callback)
{
    recorder->record_value = callback;
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_STATUS lcbmetrics_recorder_destroy(lcbmetrics_RECORDER *recorder)
{
    delete recorder;
    return LCB_SUCCESS;
}
