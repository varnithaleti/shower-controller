import React, { useEffect, useState } from "react";
import { FlatList, Text, TouchableOpacity, View } from "react-native";
import { BleManager, Characteristic, Device } from "react-native-ble-plx";
import { styles } from "../styles/HomeStyles";

const manager = new BleManager();

const SERVICE_UUID = "cb927983-3d04-4727-b54b-59d894bd4474";
const CHAR_UUID = "db4fcbed-5dbd-48c2-8fb2-5eec8e0ed26c";

export default function HomeScreen() {

  const [devices, setDevices] = useState<Device[]>([]);
  const [connectedDevice, setConnectedDevice] = useState<Device | null>(null);
  const [messages, setMessages] = useState<string[]>([]);
  const [showDevices, setShowDevices] = useState(false);

  useEffect(() => {
    return () => {
      manager.stopDeviceScan();
      void manager.destroy();
    };
  }, []);

  const scan = () => {

    setDevices([]);
    setShowDevices(true);

    manager.startDeviceScan(null, null, (error, device) => {

      if (error || !device) return;

      const name = device.name || device.localName;

      if (!name) return;

      setDevices(prev => {

        if (prev.find(d => d.id === device.id)) return prev;

        return [...prev, device];

      });

    });

  };

  const connect = async (device: Device) => {

    manager.stopDeviceScan();
    setShowDevices(false);

    const connected = await device.connect();
    await connected.discoverAllServicesAndCharacteristics();

    setConnectedDevice(connected);

    connected.monitorCharacteristicForService(
      SERVICE_UUID,
      CHAR_UUID,
      (error: Error | null, characteristic: Characteristic | null) => {

        if (!characteristic?.value) return;

        const decoded = atob(characteristic.value);

        setMessages(prev => [...prev, decoded]);

      }
    );

  };

  const disconnect = async () => {

    if (!connectedDevice) return;

    await manager.cancelDeviceConnection(connectedDevice.id);

    setConnectedDevice(null);
    setMessages([]);
    setDevices([]);
    setShowDevices(false);

  };

  return (

    <View style={styles.container}>

      <Text style={styles.title}>Shower Controller App</Text>

      {connectedDevice ? (
        <Text style={styles.connected}>
          Connected to {connectedDevice.name}
        </Text>
      ) : (
        <Text style={styles.status}>No Device Connected</Text>
      )}

      {!connectedDevice && (
        <TouchableOpacity style={styles.connectButton} onPress={scan}>
          <Text style={styles.connectText}>Connect to Device</Text>
        </TouchableOpacity>
      )}

      {connectedDevice && (
        <TouchableOpacity style={styles.disconnectButton} onPress={disconnect}>
          <Text style={styles.disconnectText}>Disconnect</Text>
        </TouchableOpacity>
      )}

      {showDevices && (

        <View style={styles.deviceList}>

          <FlatList
            data={devices}
            keyExtractor={(item) => item.id}
            renderItem={({ item }) => (

              <TouchableOpacity
                style={styles.device}
                onPress={() => connect(item)}
              >

                <Text style={styles.deviceText}>
                  {item.name || item.localName}
                </Text>

              </TouchableOpacity>

            )}
          />

        </View>

      )}

      <View style={styles.messageBox}>

        <Text style={styles.messageTitle}>Messages</Text>

        <FlatList
          data={messages}
          keyExtractor={(_, index) => index.toString()}
          renderItem={({ item }) => (
            <Text style={styles.message}>{item}</Text>
          )}
        />

      </View>

    </View>

  );

}