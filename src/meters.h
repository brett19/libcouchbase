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

#ifndef LCB_METERS_H
#define LCB_METERS_H

#ifdef __cplusplus

#include <libcouchbase/utils.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

typedef struct lcbmetrics_METER_ {
    void *cookie;
    lcbmetrics_CREATE_RECORDER new_recorder;
} lcbmetrics_METER;

struct lcbmetrics_RECORDER_ {
    void *cookie;
    lcbmetrics_RECORD_VALUE record_value;
} lcbmetrics_RECORDER;

namespace lcb
{
namespace metrics
{

class MeterManager {

} lcb_METERMANAGER;

lcb_metermanager_create()
lcb_metermanager_destroy()

struct tag {
    std::string key;
    std::string value;
};
inline bool operator==(const tag &lhs, const tag &rhs)
{
    return lhs.key == rhs.key && lhs.value == rhs.value;
}

class ValueRecorder
{
  public:
    virtual void recordValue(lcb_U64 value) = 0;
    virtual ~ValueRecorder() {}
};

class Meter
{
  public:
    virtual ValueRecorder &valueRecorder(const std::string &name, const std::vector<tag> &tags) = 0;
    virtual ~Meter() {}
    virtual void flush() {}
};

class CustomValueRecorder : public ValueRecorder
{
  public:
    CustomValueRecorder(lcbmetrics_RECORDER *recorder);
    ~CustomValueRecorder();

    void recordValue(lcb_U64 value) override;

  private:
    const lcbmetrics_RECORDER *recorder_;
};

class TracingManager
{
  public:
    TracingManager(lcb_settings_t *settings) : settings_(settings) {}

    ValueRecorder &valueRecorder(const std::string &name, const std::vector<tag> &tags)
    {
        if (!settings_->meter) {
            return
        }
    }

  private:
    const lcb_settings_t *settings_;
    std::unordered_map<std::string, CustomValueRecorder> valueRecorders_;
}

class CustomMeter : public Meter
{
  public:
    CustomMeter(const lcbmetrics_METER *meter);

    ValueRecorder &valueRecorder(const std::string &name, const std::vector<tag> &tags) override;

  private:
    const lcbmetrics_METER *meter_;
    std::unordered_map<std::string, CustomValueRecorder> valueRecorders_;
};

class AggregatingValueRecorder : public ValueRecorder
{
  public:
    AggregatingValueRecorder(const std::string &name, const std::vector<tag> &tags);
    ~AggregatingValueRecorder();

    void recordValue(lcb_U64 value) override;

    const std::string &name() const;
    const std::vector<tag> &tags() const;
    void flush(FILE *stream);

  private:
    std::string name_;
    std::vector<tag> tags_;
    lcb_HISTOGRAM *histogram_;
};

class AggregatingMeter : public Meter
{
  public:
    AggregatingMeter(lcb_INSTANCE *lcb);
    ValueRecorder &valueRecorder(const std::string &name, const std::vector<tag> &tags) override;
    void flush() override;

  private:
    lcb_INSTANCE *lcb_;
    lcb::io::Timer<AggregatingMeter, &AggregatingMeter::flush> timer_;
    std::list<AggregatingValueRecorder> valueRecorders_;
};
} // namespace metrics
} // namespace lcb

void record_kv_op_latency_store(lcb_INSTANCE *instance, mc_PACKET *request, lcb_RESPSTORE *response);
void record_kv_op_latency(const char *op, lcb_INSTANCE *instance, mc_PACKET *request);
void record_http_op_latency(const char *op, const char *svc, lcb_INSTANCE *instance, hrtime_t start);

#endif //__cplusplus
#endif // LCB_METERS_H
