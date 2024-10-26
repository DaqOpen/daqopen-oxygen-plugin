// Copyright Michael Oberhofer 2024

#ifdef WIN32
#define  _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#endif

#include "odkapi_config_item_keys.h"
#include "odkfw_custom_request_handler.h"
#include "odkfw_properties.h"
#include "odkfw_software_channel_plugin.h"
#include "odkbase_message_return_value_holder.h"
#include "odkapi_utils.h"

#include "qml.rcc.h"

#include <codecvt>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <zmq.hpp>
#include "json.hpp"

using json = nlohmann::json;

class Logger {
public:
    Logger() {
        const std::string filename="C:/Users/Public/Documents/Dewetron/Oxygen/Log/zmq-sub.txt";
        logFile.open(filename, std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Fehler beim Öffnen der Log-Datei." << std::endl;
        }
    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    void log(const std::string& message) {
        if (logFile.is_open()) {
            logFile << getCurrentTime() << " - " << message << std::endl;
        }
    }

private:
    std::ofstream logFile;

    std::string getCurrentTime() {
        std::time_t now = std::time(nullptr);
        char buffer[100];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return buffer;
    }
};

// Create Logger
Logger logger;

class ZMQSubscriber {
public:
    ZMQSubscriber()
        : context(1), subscriber(context, zmq::socket_type::sub) {
    }

    void init(const std::string& address) {
        subscriber.connect(address);
        subscriber.set(zmq::sockopt::subscribe, "");
        //subscriber.set(zmq::sockopt::rcvtimeo, 100);
    }

    std::optional<std::pair<json, std::vector<int16_t>>> receiveMessage(bool blocking = false) {
        zmq::message_t meta_msg;
        zmq::message_t data_msg;

        zmq::recv_result_t rc_meta;
        zmq::recv_result_t rc_msg;

        if (blocking == true) {
            rc_meta = subscriber.recv(meta_msg, zmq::recv_flags::none);
        }
        else {
            try {
                rc_meta = subscriber.recv(meta_msg, zmq::recv_flags::dontwait);
            }
            catch (const std::exception& e) {
                logger.log(std::string("Exception caught: ") + e.what());
                return std::nullopt;
            } 
            catch (...) {
                logger.log("Unknown exception caught.");
                return std::nullopt;
            }
            if (!rc_meta) {
                logger.log("No Message Received");
                return std::nullopt;
            }
        }
        // Receive Second Part of message (Always Blocking)
        rc_msg = subscriber.recv(data_msg, zmq::recv_flags::none);

        // Convert the Metadata
        json meta_json = json::parse(meta_msg.to_string());
        // Convert Data
        std::vector<int16_t> data_vec(static_cast<int16_t*>(data_msg.data()), static_cast<int16_t*>(data_msg.data()) + data_msg.size()/sizeof(int16_t));

        return std::make_pair(meta_json, data_vec);
    }

private:
    zmq::context_t context; // Der ZeroMQ-Kontext
    zmq::socket_t subscriber; // Der Subscriber-Socket
};

// Manifest constains necessary metadata for oxygen plugins
//   OxygenPlugin.name: unique plugin identifier; please use your (company) name to avoid name conflicts. This name is also used as a prefix in all custom config item keys.
//   OxygenPlugin.uuid: unique number (generated by a GUID/UUID generator tool) that stored in configuration files to match channels etc. to the correct plugin
static const char* PLUGIN_MANIFEST =
R"XML(<?xml version="1.0"?>
<OxygenPlugin name="DAQOPEN_ZMQ_SUB" version="1.1" uuid="CE4A5D5B-B4C1-48AB-90FA-73E532739C82">
  <Info name="DAQopen Plugin">
    <Vendor name="Michael Oberhofer"/>
    <Description>DAQopen Plugin: Subscribe to DAQopen ZMQ Publisher</Description>
  </Info>
  <Host minimum_version="7.2.1"/>
  <UsesUIExtensions/>
</OxygenPlugin>
)XML";

// A minimal translation file that maps the internal ConfigItem key to a nicer text for the user
static const char* TRANSLATION_EN =
R"XML(<?xml version="1.0"?>
<TS version="2.1" language="en" sourcelanguage="en">
    <context><name>ConfigKeys</name>
        <message><source>DAQOPEN_ZMQ_SUB/ZmqConnStr</source><translation>ZMQ Connection String</translation></message>
    </context>
</TS>
)XML";

// Keys for ConfigItems that are used to store channel settings
std::string DATA_CH_KEY_PREFIX = "DATACHANNEL_";
// Custom key (prefixed by plugin name)
static const char* KEY_ZMQ_CONN_STR = "DAQOPEN_ZMQ_SUB/ZmqConnStr";

using namespace odk::framework;

class DaqOpenZmqInstance : public SoftwareChannelInstance
{
public:

    DaqOpenZmqInstance()
        : m_zmq_conn_str(new EditableStringProperty(""))
        , m_next_tick(std::numeric_limits<uint64_t>::max())
    {
    }

    // Describe how the software channel should be shown in the "Add Channel" dialog
    static odk::RegisterSoftwareChannel getSoftwareChannelInfo()
    {
        odk::RegisterSoftwareChannel telegram;
        telegram.m_display_name = "DaqOpen ZMQ Sub";
        telegram.m_service_name = "CreateChannel";
        telegram.m_display_group = "Data Input";
        telegram.m_description = "Create Channels from DAQopen ZMQ";
        telegram.m_ui_item_add = "AddChannel";
        telegram.m_analysis_capable = false;
        return telegram;
    }

    InitResult init(const InitParams& params) override
    {
        odk::PropertyList props(params.m_properties);

        auto zmq_conn_str = props.getString("DAQOPEN_ZMQ_SUB/ZmqConnStr");
        if (!zmq_conn_str.empty())
        {
            m_zmq_conn_str->setValue(zmq_conn_str);
            update();
        }

        InitResult r(true);
        r.showChannelDetails(getRootChannel()->getLocalId());

        return r;
    }

    void updatePropertyTypes(const PluginChannelPtr& output_channel) override
    {
        ODK_UNUSED(output_channel);
    }

    void updateStaticPropertyConstraints(const PluginChannelPtr& channel) override
    {
        ODK_UNUSED(channel);
    }

    bool update() override
    {
        // Renew Connection to ZMQ Client
        subscriber.reset();
        subscriber = std::make_unique<ZMQSubscriber>();
        logger.log("Connect to " + m_zmq_conn_str->getValue());
        subscriber->init(m_zmq_conn_str->getValue());

        // Blocking wait for first message
        // TODO: Non-Blocking with Error Message; Delete Channels and make invalid
        auto result = subscriber->receiveMessage(true);
        logger.log("Update Method: ZMQ message received");

        // Extract data from message
        auto [meta_json, data_vec] = *result;
        logger.log("JSON: " + meta_json.dump());
        m_metadata = meta_json;

        // Initialize Sample ticks
        m_next_tick = std::numeric_limits<uint64_t>::max();

        // Initialize Channel Data Map
        m_channel_data_map.clear();
        m_data_column_map.clear();

        for (auto& [ch_name, ch_prop] : meta_json["daq_info"]["channel"].items()) {
            m_channel_data_map[ch_name] = {};
            m_data_column_map[ch_name] = meta_json["data_columns"][ch_prop["ai_pin"]];
            
            // Check if channel exists in OXYGEN
            auto existing_channel = getOutputChannelByKey(DATA_CH_KEY_PREFIX + ch_name);
            if (existing_channel) {
                m_channel_map[ch_name] = existing_channel;
            }

            // Add Channel if not in OXYGEN
            if (m_channel_map.find(ch_name) == m_channel_map.end()) {
                // Add new Channel if it does not exists
                m_channel_map[ch_name] = addOutputChannel(DATA_CH_KEY_PREFIX + ch_name);
                m_channel_map[ch_name]->setDefaultName(ch_name)
                .setSampleFormat(
                    odk::ChannelDataformat::SampleOccurrence::SYNC,
                    odk::ChannelDataformat::SampleFormat::DOUBLE,
                    1)
                .setDeletable(true)
                ;
            }
            auto min_range = (static_cast<double>(m_metadata["daq_info"]["board"]["adc_range"][0]) * 
                              static_cast<double>(m_metadata["daq_info"]["channel"][ch_name]["gain"]) - 
                              static_cast<double>(m_metadata["daq_info"]["channel"][ch_name]["offset"]));
            auto max_range = (static_cast<double>(m_metadata["daq_info"]["board"]["adc_range"][1]) * 
                              static_cast<double>(m_metadata["daq_info"]["channel"][ch_name]["gain"]) - 
                              static_cast<double>(m_metadata["daq_info"]["channel"][ch_name]["offset"]));
            // Configure Channel
            m_channel_map[ch_name]->setSamplerate({meta_json["daq_info"]["board"]["samplerate"], "Hz"})
            .setSimpleTimebase(meta_json["daq_info"]["board"]["samplerate"])
            .setRange({min_range, max_range, "", ""})
            .setUnit(m_metadata["daq_info"]["channel"][ch_name]["unit"])
            ;
            
        }

        // Get all existing OXYGEN Channels and remove which dont't exists anymore
        auto existing_channels = getOutputChannels();
        for (auto& channel : existing_channels) {
            const auto& key_property = std::dynamic_pointer_cast<EditableStringProperty>(channel->getProperty("SoftwareChannelInstanceKey"));
            logger.log("Channel existing: " + channel->getName() + " with Key: " + key_property->getValue());
            // Remove non-existant channels (Only Data Channels)
            // Check if Data Channel
            if (key_property->getValue().rfind(DATA_CH_KEY_PREFIX, 0) == 0) {
                auto data_ch_name = key_property->getValue().substr(DATA_CH_KEY_PREFIX.length());
                logger.log("Search for Channel with Key: " + data_ch_name);
                if  (m_channel_map.find(data_ch_name) == m_channel_map.end()) {
                    logger.log("Remove Channel with Key: " + key_property->getValue());
                    removeOutputChannel(channel);
                }
            }
        }

        logger.log("m_channel_data_map_size: " + std::to_string(m_channel_data_map.size()));
        logger.log("m_channel_map_size: " + std::to_string(m_channel_map.size()));

        return true;
    }

    void create(odk::IfHost* host) override
    {
        ODK_UNUSED(host);

        getRootChannel()->setDefaultName("DAQopen ZMQ Sub")
            .setDeletable(true)
            .addProperty(KEY_ZMQ_CONN_STR, m_zmq_conn_str);
    }

    bool configure(
        const odk::UpdateChannelsTelegram& request,
        std::map<std::uint32_t, std::uint32_t>& channel_id_map) override
    {
        configureFromTelegram(request, channel_id_map);

        logger.log("Configure Finished");
        return true;
    }

    void prepareProcessing(odk::IfHost* host) override
    {
        const auto ts = getMasterTimestamp(host);
        auto samplerate = m_channel_map.begin()->second->getSamplerateProperty(); // TODO: Alternative when no Channel
        const auto rate_factor = samplerate->getValue().m_val / ts.m_frequency;
        m_next_tick = static_cast<std::uint64_t>(ts.m_ticks* rate_factor);
    }

    void process(ProcessingContext& context, odk::IfHost *host) override
    {
        ODK_UNUSED(context);

        auto ts = getMasterTimestamp(host);
        auto acq_start_ts = getAcquisitionStartTime(host);

        auto tick = m_next_tick;
        auto samplerate = m_channel_map.begin()->second->getSamplerateProperty(); // TODO: Alternative when no Channel?
        const auto rate_factor = samplerate->getValue().m_val / ts.m_frequency;
        std::uint64_t target_tick = static_cast<std::uint64_t>(ts.m_ticks * rate_factor);

        // Iterate thru all queued zmq messages
        while(true) {
            
            auto result = subscriber->receiveMessage(false);

            if (result) {
                auto [meta_json, data_vec] = *result;
                // TODO: Update Channels if metadata changed
                if (meta_json["daq_info"] != m_metadata["daq_info"]) {
                    logger.log("Metadata Differs!!!");
                }
                // Iterate all Channels
                for (const auto& [ch_name, ch_data_vec] : m_channel_data_map) {
                    m_channel_data_map[ch_name].clear();
                    for (size_t idx = m_data_column_map[ch_name]; idx < data_vec.size(); idx += m_channel_data_map.size()) {
                        m_channel_data_map[ch_name].push_back(static_cast<double>(data_vec[idx]*
                                            static_cast<double>(m_metadata["daq_info"]["channel"][ch_name]["gain"]) - 
                                            static_cast<double>(m_metadata["daq_info"]["channel"][ch_name]["offset"]))); 
                    }
                    addSamples(host, m_channel_map[ch_name]->getLocalId(), tick, &m_channel_data_map[ch_name][0], sizeof(double) * m_channel_data_map[ch_name].size());
                }
                tick += m_channel_data_map.begin()->second.size();
                logger.log("Added new Samples: m_values.size()=" + std::to_string(m_channel_data_map.begin()->second.size()) + " at tick=" + std::to_string(tick) + " with acq_start_ts=" + std::to_string(acq_start_ts.m_nanoseconds_since_1970));
            }
            else {
                break;
            }
        }

        m_next_tick = tick;
    }

private:
    std::shared_ptr<EditableStringProperty> m_zmq_conn_str;

    std::uint64_t m_next_tick; // timestamp of the next sample that will be generated in doProcess()

    //ZMQSubscriber subscriber;
    std::unique_ptr<ZMQSubscriber> subscriber = std::make_unique<ZMQSubscriber>();

    std::unordered_map<std::string, PluginChannelPtr> m_channel_map;
    std::unordered_map<std::string, std::vector<double>> m_channel_data_map;
    std::unordered_map<std::string, std::uint8_t> m_data_column_map;
    json m_metadata = {{"daq_info", {}}};

};

class DaqOpenZmqSubscriberPlugin : public SoftwareChannelPlugin<DaqOpenZmqInstance>
{
public:
    void registerResources() final
    {
        addTranslation(TRANSLATION_EN);
        addQtResources(plugin_resources::QML_RCC_data, plugin_resources::QML_RCC_size);
    }

};

OXY_REGISTER_PLUGIN1("DAQOPEN_ZMQ_SUB", PLUGIN_MANIFEST, DaqOpenZmqSubscriberPlugin);

