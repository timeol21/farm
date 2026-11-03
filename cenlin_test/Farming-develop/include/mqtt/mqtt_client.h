#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include "mqtt/async_client.h"
#include "config/config.h"

class HallService;

class MqttClient : public virtual mqtt::callback{
private:
	MqttClient();
	~MqttClient();
private:
	std::unique_ptr<mqtt::async_client> client_;
	std::atomic<bool> isConnected_{ false };
	std::vector<std::string> subscribedTopics_;
	HallService* hallService;
public:
	static MqttClient& getInstance();
	MqttClient(const MqttClient&) = delete;
	MqttClient& operator=(const MqttClient&) = delete;

	bool connect();

	bool subscribe(const std::string& topic);

	bool publish(const std::string& topic, const std::string& payload);
	void disconnect();

	void setHallService(HallService* service);

	void connected(const std::string& cause) override;
	void connection_lost(const std::string& cause) override;
	void message_arrived(mqtt::const_message_ptr msg) override;
};