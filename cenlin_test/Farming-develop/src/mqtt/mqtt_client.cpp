#include "mqtt/mqtt_client.h"
#include <iostream>
#include "logger/logger.h"
#include "config/config_parser.h"
#include "service/hall_service.h"

MqttClient::MqttClient() :hallService(nullptr) {}

MqttClient::~MqttClient() {
	disconnect();
}

MqttClient& MqttClient::getInstance() {
	static MqttClient instance;
	return instance;
}

bool MqttClient::connect() {
	const AppConfig& cfg = ConfigParser::getInstance().getConfig();
	const MqttConfig& mqttCfg = cfg.mqtt;

	std::string broker = mqttCfg.broker.empty() ? "tcp://localhost:1883" : mqttCfg.broker;
	std::string clientId = mqttCfg.client_id.empty() ? "mqtt_client" : mqttCfg.client_id;

	try{
		client_ = std::make_unique<mqtt::async_client>(broker, clientId);
		client_->set_callback(*this);

		mqtt::connect_options connOpts;
		connOpts.set_clean_session(true);
		connOpts.set_automatic_reconnect(true);
		connOpts.set_connect_timeout(10);
		connOpts.set_keep_alive_interval(20);

		client_->connect(connOpts)->wait();

		isConnected_ = true;
		LOG_INFO("[MQTT] Connected to " + broker + " with client ID: " + clientId);

		for(const auto& sub : mqttCfg.subscriptions){
			std::string topic = sub.topic;
			int qos = sub.qos;

			if(!topic.empty()){
				client_->subscribe(topic, qos)->wait();
				LOG_INFO("[MQTT] Subscribed to topic: " + topic + " with QoS: " + std::to_string(qos));
				subscribedTopics_.push_back(topic);
			}
		}
		return true;
	} catch(const mqtt::exception& e){
		LOG_ERROR("[MQTT] Connection failed: " + std::string(e.what()));
		return false;
	}
}

bool MqttClient::subscribe(const std::string& topic) {
	if (!isConnected_) {
		LOG_WARNING("[MQTT] Cannot subscribe, client is not connected");
		return false;
	}
	try {
		client_->subscribe(topic, 1)->wait();
		LOG_INFO("[MQTT] Subscribed: " + topic);
		subscribedTopics_.push_back(topic);
		return true;
	}
	catch (...) {
		return false;
	}
}

bool MqttClient::publish(const std::string& topic, const std::string& payload) {
	if (!isConnected_) {
		return false;
	}
	try {
		auto msg = mqtt::make_message(topic, payload);
		msg->set_qos(1);
		client_->publish(msg);
		return true;
	}
	catch (...) {
		return false;
	}
}

void MqttClient::disconnect() {
	try {
		if (client_ && isConnected_) {
			client_->disconnect()->wait();
			isConnected_ = false;
		}
	}
	catch(...){}
}

void MqttClient::setHallService(HallService* service) {
	hallService = service;
}

void MqttClient::message_arrived(mqtt::const_message_ptr msg) {
	std::string topic = msg->get_topic();
	std::string payload = msg->to_string();
	// LOG_INFO("[MQTT] Message arrived. Topic: " + topic);
	if (!hallService) {
		LOG_WARNING("[MQTT] No HallService set, cannot handle message");
		return;
	}
	// 构建Request请求体
	Request req;
	req.source = "MQTT";

	try {
		auto j = nlohmann::json::parse(payload);
		req.cmd = j.at("cmd").get<std::string>();
		req.payload = j.dump();
		if (j.contains("replyTo")) {
			req.replyTo = j["replyTo"].get<std::string>();
		}
		else {
			req.replyTo = topic + "/reply";
		}
	}
	catch (...) {
		req.replyTo = topic + "/reply";
	}
	// std::cout<<"[MQTT] Parsed Request: cmd="<<req.cmd<<", payload="<<req.payload<<", replyTo="<<req.replyTo<<std::endl;
	hallService->handleRequest(req);
}

void MqttClient::connected(const std::string& cause) {
	LOG_INFO("[MQTT] Connected callback: " + cause);

	isConnected_ = true;
	for (const auto& topic : subscribedTopics_) {
		try {
			client_->subscribe(topic, 1);
		}catch(...){}
	}
}

void MqttClient::connection_lost(const std::string& cause) {
	isConnected_ = false;
	LOG_ERROR("[MQTT] Connection lost: " + cause);
}