# esp-wifi-connect (vendored)

Upstream: [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect) v3.2.2.

Vendored for **deep-dog** (see `main/boards/deep-dog/swrs/net/N03-wifi-portal-mqtt-broker.md`):

- `WifiManagerConfig.show_mqtt_broker_config`
- Advanced portal fields: board MQTT `broker_host` / `broker_port` → NVS `deep_dog_mqtt`
- Wired via `WifiConfigurationAp::SetShowMqttBrokerConfig`

Override in `main/idf_component.yml`:

```yaml
78/esp-wifi-connect:
  path: ../components/esp-wifi-connect
```
