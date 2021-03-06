/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2012-2020 Couchbase, Inc.
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

#include "histogram.h"
#include <string>
using namespace cbc;
using std::string;

void Histogram::install(lcb_INSTANCE *inst, FILE *out)
{
    lcb_STATUS rc;
    output = out;
    lcb_enable_timings(inst);
    rc = lcb_cntl(inst, LCB_CNTL_GET, LCB_CNTL_KVTIMINGS, &hg);
    lcb_assert(rc == LCB_SUCCESS);
    lcb_assert(hg != nullptr);
    (void)rc;
}

void Histogram::installStandalone(FILE *out)
{
    if (hg != nullptr) {
        return;
    }
    hg = lcb_histogram_create();
    output = out;
}

#ifdef _WIN32
#define flockfile(x) (void)0
#define funlockfile(x) (void)0
#endif

void Histogram::write()
{
    if (hg == nullptr) {
        return;
    }
    flockfile(output);
    lcb_histogram_print(hg, output);
    funlockfile(output);
}

void Histogram::record(lcb_U64 duration)
{
    if (hg == nullptr) {
        return;
    }
    lcb_histogram_record(hg, duration);
}
