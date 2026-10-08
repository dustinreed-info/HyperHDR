#pragma once

#include <led-drivers/LedDevice.h>
#include "ProviderRestApi.h"
#include "ProviderUdp.h"
#include <array>

class DriverNetWled : public ProviderUdp
{
	Q_OBJECT

public:
	explicit DriverNetWled(const QJsonObject& deviceConfig);
	static LedDevice* construct(const QJsonObject& deviceConfig);

	QJsonObject discover(const QJsonObject& params) override;

protected:
	bool init(QJsonObject deviceConfig) override;
	int writeFiniteColors(const std::vector<ColorRgb>& ledValues) override;
	bool powerOn() override;
	bool powerOff() override;

private:
	bool initRestAPI(QString hostname, int port);
	QString getOnOffRequest(bool isOn) const;
	std::unique_ptr<ProviderRestApi> _restApi;

	QString _hostname;
	int		_apiPort;
	int		_warlsStreamPort;
	bool	_overrideBrightness;
	int		_brightnessLevel;
	bool	_restoreConfig;
	QJsonDocument _configBackup;

	// ProviderUdp copies each datagram synchronously, so this storage can be reused.
	static constexpr std::size_t MAX_UDP_PACKET_SIZE = 1472;
	std::array<uint8_t, MAX_UDP_PACKET_SIZE> _udpPacket{};

	static bool isRegistered;
};
