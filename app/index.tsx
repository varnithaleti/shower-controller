import { Buffer } from "buffer";
import { makeRedirectUri, useAuthRequest } from "expo-auth-session";
import * as WebBrowser from "expo-web-browser";
import React, { useEffect, useRef, useState } from "react";
import { FlatList, Text, TouchableOpacity, View } from "react-native";
import { BleManager, Characteristic, Device } from "react-native-ble-plx";
import { styles } from "../styles/HomeStyles";

const manager = new BleManager();

const SERVICE_UUID = "cb927983-3d04-4727-b54b-59d894bd4474";
const CHAR_UUID = "db4fcbed-5dbd-48c2-8fb2-5eec8e0ed26c";

WebBrowser.maybeCompleteAuthSession();

const discovery = {
  authorizationEndpoint: "https://accounts.spotify.com/authorize",
  tokenEndpoint: "https://accounts.spotify.com/api/token",
};

export default function HomeScreen() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [connectedDevice, setConnectedDevice] = useState<Device | null>(null);
  const [messages, setMessages] = useState<string[]>([]);
  const [showDevices, setShowDevices] = useState(false);
  const redirectUri = makeRedirectUri({
    scheme: "showercontroller",
    path: "callback",
  });
  const [spotifyToken, setSpotifyToken] = useState<string | null>(null);
  const tokenRef = useRef<string | null>(null);
  const userIdRef = useRef<string | null>(null);

  const playlistsRef = useRef<any[]>([]);
  const tracksRef = useRef<any[]>([]);
  const activePlaylistUriRef = useRef<string | null>(null);

  const [request, response, promptAsync] = useAuthRequest(
    {
      clientId: "edf4a8aa3aad49cea5aa54baa213ba7e",
      scopes: [
        "user-read-playback-state", // To see what's playing
        "user-modify-playback-state", // To play, pause, skip, and change volume
        "playlist-read-private",
        "playlist-read-collaborative",
      ],
      usePKCE: true,
      redirectUri: redirectUri,
      extraParams: {
        show_dialog: "true",
      },
    },
    discovery,
  );

  useEffect(() => {
    if (response?.type === "success" && request?.codeVerifier) {
      const { code } = response.params;

      const fetchToken = async () => {
        try {
          const tokenResult = await fetch(discovery.tokenEndpoint, {
            method: "POST",
            headers: {
              "Content-Type": "application/x-www-form-urlencoded",
            },
            body: `grant_type=authorization_code&code=${code}&redirect_uri=${encodeURIComponent(redirectUri)}&client_id=edf4a8aa3aad49cea5aa54baa213ba7e&code_verifier=${request.codeVerifier}`,
          });

          const data = await tokenResult.json();

          console.log("SUCCESS! Here is the token:", data.access_token);
          console.log("GRANTED SCOPES:", data.scope);
          setSpotifyToken(data.access_token);
          tokenRef.current = data.access_token;

          try {
            const profileRes = await fetch("https://api.spotify.com/v1/me", {
              headers: { Authorization: `Bearer ${data.access_token}` },
            });
            const profileData = await profileRes.json();
            userIdRef.current = profileData.id;
            console.log("Logged in as User ID:", userIdRef.current);
          } catch (profileError) {
            console.error("Failed to fetch user profile:", profileError);
          }
        } catch (error) {
          console.error("Failed to fetch token:", error);
        }
      };

      fetchToken();
    }
  }, [response, request, redirectUri]);

  useEffect(() => {
    return () => {
      manager.stopDeviceScan();
      void manager.destroy();
    };
  }, []);

  const sendPlaylistsToHardware = async (device: Device) => {
    if (!tokenRef.current) return;
    try {
      // Get user's top 5 playlists
      const response = await fetch(
        "https://api.spotify.com/v1/me/playlists?limit=50",
        {
          headers: { Authorization: `Bearer ${tokenRef.current}` },
        },
      );
      const data = await response.json();

      if (!data.items) return;

      const ownedPlaylists = data.items.filter(
        (playlist: any) => playlist.owner?.id === userIdRef.current,
      );

      console.log(`Filtered down to ${ownedPlaylists.length} owned playlists.`);

      // Save to ref so the BLE listener can see them later
      playlistsRef.current = ownedPlaylists;
      await sendPlaylistChunk(device, 0);
    } catch (error) {
      console.error("Error sending playlists:", error);
    }
  };

  const sendTrackChunk = async (device: Device, startIndex: number) => {
    const tracks = tracksRef.current;

    if (!tracks || tracks.length === 0) {
      console.log("Safety Check: Spotify didn't return any tracks.");
      return;
    }

    // Slice 5 tracks from the requested starting point
    const chunk = tracks.slice(startIndex, startIndex + 5);
    console.log("FIRST ITEM IN CHUNK:", JSON.stringify(chunk[0], null, 2));
    // Safely grab the track names (Spotify wraps them in a track object)
    const namesString = chunk
      .map((item: any) => {
        const rawName = item?.item?.name || "Unknown";
        return rawName.replace(/[^\x20-\x7E]/g, "").trim();
      })
      .join("|");
    const payload = `T:${namesString}`;
    const encodedPayload = Buffer.from(payload, "utf-8").toString("base64");

    try {
      await device.writeCharacteristicWithResponseForService(
        SERVICE_UUID,
        CHAR_UUID,
        encodedPayload,
      );
      console.log(`Sent tracks ${startIndex} to ${startIndex + 5}:`, payload);
    } catch (error) {
      console.error("Error sending track chunk:", error);
    }
  };

  const sendPlaylistChunk = async (device: Device, startIndex: number) => {
    const playlists = playlistsRef.current;

    if (!playlists || playlists.length === 0) {
      console.log("Safety Check: No playlists found in memory.");
      return;
    }

    // Slice 5 playlists from the requested starting point
    const chunk = playlists.slice(startIndex, startIndex + 5);

    // Sanitize and format
    const namesString = chunk
      .map((playlist: any) => {
        const rawName = playlist.name || "Unknown";
        return rawName.replace(/[^\x20-\x7E]/g, "").trim();
      })
      .join("|");

    const payload = `P:${namesString}`;
    const encodedPayload = Buffer.from(payload, "utf-8").toString("base64");

    try {
      await device.writeCharacteristicWithResponseForService(
        SERVICE_UUID,
        CHAR_UUID,
        encodedPayload,
      );
      console.log(`Sent playlists ${startIndex} to ${startIndex + 5}`);
    } catch (error) {
      console.error("Error sending playlist chunk:", error);
    }
  };

  const fetchAndSendCurrentlyPlaying = async (device: Device) => {
    if (!tokenRef.current) return;

    try {
      // 1. Ask Spotify what is playing right now
      const res = await fetch(
        "https://api.spotify.com/v1/me/player/currently-playing",
        {
          headers: { Authorization: `Bearer ${tokenRef.current}` },
        },
      );

      // Status 204 means Spotify is open but nothing is actively playing
      if (res.status === 204 || res.status > 400) {
        console.log("Nothing is currently playing on Spotify.");
        return;
      }

      const data = await res.json();

      if (!data.item) return;

      // 2. Extract the names
      const rawSongName = data.item.name || "Unknown";
      const rawArtistName = data.item.artists?.[0]?.name || "Unknown";

      const progressSec = Math.floor(data.progress_ms / 1000);
      const durationSec = Math.floor(data.item.duration_ms / 1000);

      // 3. Sanitize out emojis so the ESP32 TFT screen doesn't crash
      const cleanSong = rawSongName.replace(/[^\x20-\x7E]/g, "").trim();
      const cleanArtist = rawArtistName.replace(/[^\x20-\x7E]/g, "").trim();

      // 4. Format the payload with an "N:" prefix (for Now Playing) and a pipe delimiter
      const payload = `N:${cleanSong}|${cleanArtist}|${progressSec}|${durationSec}`;
      const encodedPayload = Buffer.from(payload, "utf-8").toString("base64");

      // 5. Send it over Bluetooth
      await device.writeCharacteristicWithResponseForService(
        SERVICE_UUID,
        CHAR_UUID,
        encodedPayload,
      );

      console.log("Sent Now Playing Update:", payload);
    } catch (error) {
      console.error("Error fetching currently playing track:", error);
    }
  };

  const scan = () => {
    setDevices([]);
    setShowDevices(true);

    manager.startDeviceScan(null, null, (error, device) => {
      if (error || !device) return;

      const name = device.name || device.localName;

      if (!name) return;

      setDevices((prev) => {
        if (prev.find((d) => d.id === device.id)) return prev;

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
    await sendPlaylistsToHardware(connected);

    connected.monitorCharacteristicForService(
      SERVICE_UUID,
      CHAR_UUID,
      async (error: Error | null, characteristic: Characteristic | null) => {
        if (!characteristic?.value) return;

        const decoded = Buffer.from(characteristic.value, "base64")
          .toString("utf-8")
          .trim()
          .toUpperCase();
        setMessages((prev) => [...prev, decoded]);

        if (decoded.startsWith("LOAD_PLAYLIST:")) {
          const index = parseInt(decoded.split(":")[1]);
          const targetPlaylist = playlistsRef.current[index];

          if (targetPlaylist) {
            console.log(`Opening playlist folder: ${targetPlaylist.name}`);

            activePlaylistUriRef.current = targetPlaylist.uri;

            let allTracks: any[] = [];

            // Start with the base URL for the first 100 songs
            // (Make sure to use your correct Spotify API URL here)
            let nextUrl: string | null =
              `https://api.spotify.com/v1/playlists/${targetPlaylist.id}/items`;

            // Keep fetching as long as Spotify provides a "next" URL
            while (nextUrl) {
              const res: Response = await fetch(nextUrl, {
                headers: { Authorization: `Bearer ${tokenRef.current}` },
              });

              const data = await res.json();

              // Add this newly downloaded batch of songs to our master list
              if (data.items) {
                allTracks = [...allTracks, ...data.items];
              }

              // Spotify automatically provides the URL for the next 100 songs!
              // If there are no more songs, data.next will be null, breaking the loop.
              nextUrl = data.next;
            }

            if (allTracks.length === 0) {
              console.log("Safety Check: No tracks returned.");
              return;
            }

            console.log(
              `Successfully downloaded all ${allTracks.length} tracks!`,
            );

            tracksRef.current = allTracks;

            // 3. Send the first 5 tracks to the hardware
            sendTrackChunk(connected, 0);
          }
        } else if (decoded.startsWith("PLAY_TRACK:")) {
          const trackIndex = parseInt(decoded.split(":")[1]);

          // Ensure we have a playlist folder open
          if (activePlaylistUriRef.current) {
            console.log(
              `Playing track index ${trackIndex} from active playlist.`,
            );

            // Tell Spotify the Context (the playlist) and the Offset (the song index)
            spotifyCommand("play", "PUT", {
              context_uri: activePlaylistUriRef.current,
              offset: { position: trackIndex },
            });

            const trackData = tracksRef.current[trackIndex];

            if (trackData) {
              const rawSongName = trackData?.item?.name || "Unknown";
              const rawArtistName =
                trackData?.item?.artists?.[0]?.name || "Unknown";

              // We know progress is 0 because we just hit play!
              const durationSec = Math.floor(
                (trackData?.item?.duration_ms || 0) / 1000,
              );

              const cleanSong = rawSongName.replace(/[^\x20-\x7E]/g, "").trim();
              const cleanArtist = rawArtistName
                .replace(/[^\x20-\x7E]/g, "")
                .trim();

              const payload = `N:${cleanSong}|${cleanArtist}|0|${durationSec}`;
              const encodedPayload = Buffer.from(payload, "utf-8").toString(
                "base64",
              );

              // 3. Send it to the ESP32 with zero network delay
              connected.writeCharacteristicWithResponseForService(
                SERVICE_UUID,
                CHAR_UUID,
                encodedPayload,
              );

              console.log("Sent Instant Now Playing Update:", payload);
            }
          }
        } else if (decoded === "REFRESH_NOW_PLAYING") {
          console.log("Song ended naturally. Fetching the next track...");

          // Wait 2 seconds for Spotify to fully switch tracks, then fetch!
          setTimeout(() => {
            fetchAndSendCurrentlyPlaying(connected);
          }, 2000);
        } else if (decoded.startsWith("GET_PAGE:")) {
          const index = parseInt(decoded.split(":")[1]);
          sendTrackChunk(connected, index);
        } else if (decoded.startsWith("GET_PL_PAGE:")) {
          const index = parseInt(decoded.split(":")[1]);
          sendPlaylistChunk(connected, index);
        } else {
          switch (decoded) {
            case "PLAY":
              spotifyCommand("play", "PUT");
              break;
            case "PAUSE":
              spotifyCommand("pause", "PUT");
              break;
            case "NEXT":
              spotifyCommand("next", "POST");
              setTimeout(() => fetchAndSendCurrentlyPlaying(connected), 2000);
              break;
            case "PREV":
              spotifyCommand("previous", "POST");
              setTimeout(() => fetchAndSendCurrentlyPlaying(connected), 2000);
              break;
            default:
              console.log("Unknown hardware command:", decoded);
          }
        }
      },
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

  const getAvailableDeviceId = async () => {
    try {
      const response = await fetch(
        "https://api.spotify.com/v1/me/player/devices",
        {
          headers: {
            Authorization: `Bearer ${tokenRef.current}`,
          },
        },
      );
      const data = await response.json();
      console.log("SPOTIFY SEES THESE DEVICES:", JSON.stringify(data, null, 2));

      // If Spotify finds devices, return the ID of the first one available
      if (data.devices && data.devices.length > 0) {
        return data.devices[0].id;
      }
      return null;
    } catch (error) {
      console.error("Error fetching devices:", error);
      return null;
    }
  };

  const spotifyCommand = async (
    endpoint: string,
    method: string = "POST",
    bodyParams: any = null,
  ) => {
    // 1. Check if we have the token
    if (!tokenRef.current) {
      console.log("Cannot send command: No Spotify token available.");
      return;
    }

    try {
      let url = `https://api.spotify.com/v1/me/player/${endpoint}`;

      // 2. If we are trying to PLAY, explicitly find a device to wake it up
      if (endpoint === "play") {
        const deviceId = await getAvailableDeviceId();
        if (deviceId) {
          // Attach the specific device ID to the end of the URL
          url = `${url}?device_id=${deviceId}`;
        } else {
          console.log(
            "Spotify is completely closed. Please open the Spotify app first.",
          );
          return;
          // Note: The user MUST have the Spotify app open in the background for the API to see it.
        }
      }

      const fetchOptions: any = {
        method: method,
        headers: {
          Authorization: `Bearer ${tokenRef.current}`,
          "Content-Type": "application/json",
        },
      };

      // Add the playlist URI to the request if we passed one
      if (bodyParams) {
        fetchOptions.body = JSON.stringify(bodyParams);
      }

      const response = await fetch(url, fetchOptions);

      // 3. Handle the response
      if (!response.ok) {
        const errorText = await response.text();
        console.error(`Spotify API Error (${response.status}):`, errorText);
      } else {
        console.log(`Successfully triggered: ${endpoint}`);
      }
    } catch (error) {
      console.error("Network error sending Spotify command:", error);
    }
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Shower Controller App</Text>

      {!spotifyToken ? (
        <TouchableOpacity
          style={[styles.connectButton, { backgroundColor: "#1DB954" }]}
          disabled={!request}
          onPress={() => promptAsync()}
        >
          <Text style={styles.connectText}>Login with Spotify</Text>
        </TouchableOpacity>
      ) : (
        <Text style={styles.connected}>Spotify Linked Successfully!</Text>
      )}

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
          renderItem={({ item }) => <Text style={styles.message}>{item}</Text>}
        />
      </View>
    </View>
  );
}
