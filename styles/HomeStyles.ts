import { StyleSheet } from "react-native";

export const styles = StyleSheet.create({

  container: {
    flex: 1,
    alignItems: "center",
    paddingTop: 80,
    backgroundColor: "#f5f5f5",
  },

  title: {
    fontSize: 32,
    fontWeight: "bold",
    marginBottom: 20,
  },

  status: {
    fontSize: 18,
    marginBottom: 20,
  },

  connected: {
    fontSize: 18,
    color: "green",
    marginBottom: 20,
  },

  connectButton: {
    backgroundColor: "#007AFF",
    paddingHorizontal: 30,
    paddingVertical: 12,
    borderRadius: 8,
    marginBottom: 20,
  },

  connectText: {
    color: "white",
    fontSize: 18,
  },

  deviceList: {
    width: "90%",
    backgroundColor: "white",
    borderRadius: 10,
    padding: 10,
    marginBottom: 20,
    maxHeight: 200,
  },

  device: {
    padding: 15,
    borderBottomWidth: 1,
    borderColor: "#eee",
  },

  deviceText: {
    fontSize: 18,
  },

  messageBox: {
    width: "90%",
    backgroundColor: "white",
    borderRadius: 10,
    padding: 15,
    marginTop: 20,
    flex: 1,
  },

  messageTitle: {
    fontSize: 22,
    marginBottom: 10,
    fontWeight: "600",
  },

  message: {
    fontSize: 18,
    marginBottom: 5,
  },

  disconnectButton: {
    backgroundColor: "#FF3B30",
    paddingHorizontal: 30,
    paddingVertical: 12,
    borderRadius: 8,
    marginBottom: 20,
    },

    disconnectText: {
    color: "white",
    fontSize: 18,
    },

});