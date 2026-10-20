import { type JSX } from "preact";
import { useCallback, useEffect, useState } from "preact/hooks";

const CONFIG_PATH = "/api/config/Outlook Guardian/Bluetooth Remote";

interface DeviceProfile {
  address: string;
  address_type: number;
  name: string;
  mappings: string[];
  last_hid: string;
}

interface NearbyDevice {
  address: string;
  address_type: number;
  name: string;
  rssi: number;
}

interface BluetoothConfig {
  scan_seconds: number;
  selected: string;
  connected: string;
  scanning: boolean;
  profiles: DeviceProfile[];
  nearby: NearbyDevice[];
}

interface ConfigEnvelope {
  config: BluetoothConfig;
}

async function requestButton(name: string): Promise<void> {
  const response = await fetch(`/api/buttons/${encodeURIComponent(name)}`, {
    method: "POST",
  });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
}

async function saveConfig(config: BluetoothConfig): Promise<void> {
  const response = await fetch(CONFIG_PATH, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config),
  });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
}

function useBluetoothConfig(): {
  config: BluetoothConfig | null;
  error: string;
  refresh: () => Promise<void>;
  update: (value: BluetoothConfig) => void;
} {
  const [config, setConfig] = useState<BluetoothConfig | null>(null);
  const [error, setError] = useState("");
  const refresh = useCallback(async () => {
    try {
      const response = await fetch(CONFIG_PATH);
      if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
      const envelope = (await response.json()) as ConfigEnvelope;
      setConfig(envelope.config);
      setError("");
    } catch (reason) {
      setError(String(reason));
    }
  }, []);
  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => void refresh(), 2000);
    return () => window.clearInterval(timer);
  }, [refresh]);
  return { config, error, refresh, update: setConfig };
}

async function selectAndRun(
  config: BluetoothConfig,
  address: string,
  action: string,
): Promise<void> {
  await saveConfig({ ...config, selected: address });
  await requestButton(action);
}

function MappingEditor({
  config,
  profile,
  onSaved,
}: {
  config: BluetoothConfig;
  profile: DeviceProfile;
  onSaved: () => Promise<void>;
}): JSX.Element {
  const [mappings, setMappings] = useState<string[]>(profile.mappings ?? []);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState("");

  async function save(): Promise<void> {
    setSaving(true);
    setMessage("");
    try {
      const profiles = config.profiles.map((item) =>
        item.address === profile.address ? { ...item, mappings } : item,
      );
      await saveConfig({ ...config, profiles });
      setMessage("Saved");
      await onSaved();
    } catch (reason) {
      setMessage(`Save failed: ${String(reason)}`);
    } finally {
      setSaving(false);
    }
  }

  return (
    <div className="guardian-mappings">
      <div className="small text-body-secondary mb-2">
        Signal K PUT mappings: button code = path | JSON value | optional words
        to speak
      </div>
      {mappings.map((mapping, index) => (
        <div className="input-group input-group-sm mb-2" key={index}>
          <input
            className="form-control font-monospace"
            aria-label={`Signal K mapping ${index + 1}`}
            value={mapping}
            placeholder="0100=electrical.switches.anchorLight.state|true|Anchor light on"
            onInput={(event) => {
              const next = [...mappings];
              next[index] = event.currentTarget.value;
              setMappings(next);
            }}
          />
          <button
            className="btn btn-outline-danger"
            type="button"
            onClick={() => setMappings(mappings.filter((_, i) => i !== index))}
          >
            Remove
          </button>
        </div>
      ))}
      <div className="d-flex flex-wrap gap-2 align-items-center">
        <button
          className="btn btn-outline-secondary btn-sm"
          type="button"
          onClick={() =>
            setMappings([
              ...mappings,
              `${profile.last_hid || "BUTTON_CODE"}=navigation.example|true`,
            ])
          }
        >
          Add Signal K PUT
        </button>
        <button
          className="btn btn-primary btn-sm"
          type="button"
          disabled={saving}
          onClick={() => void save()}
        >
          {saving ? "Saving…" : "Save mappings"}
        </button>
        {message && <span className="small text-body-secondary">{message}</span>}
      </div>
    </div>
  );
}

function DeviceCard({
  config,
  profile,
  editable,
  refresh,
}: {
  config: BluetoothConfig;
  profile: DeviceProfile;
  editable: boolean;
  refresh: () => Promise<void>;
}): JSX.Element {
  const [busy, setBusy] = useState(false);
  const [expanded, setExpanded] = useState(editable);
  const connected = config.connected === profile.address;
  const inRange = config.nearby.some((item) => item.address === profile.address);

  async function action(name: string): Promise<void> {
    setBusy(true);
    try {
      await selectAndRun(config, profile.address, name);
      window.setTimeout(() => void refresh(), 800);
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="guardian-device">
      <div className="guardian-device-main">
        <div className="guardian-device-icon" aria-hidden="true">⌨</div>
        <div className="guardian-device-name">
          <div>{profile.name || "Bluetooth remote"}</div>
          <div className={connected ? "text-success small" : "text-body-secondary small"}>
            {connected ? "Connected" : inRange ? "Not connected · in range" : "Not connected"}
          </div>
          <div className="guardian-address">{profile.address}</div>
        </div>
        <div className="d-flex gap-2 flex-wrap justify-content-end">
          <button
            className="btn btn-outline-secondary btn-sm"
            type="button"
            disabled={busy || !profile.last_hid}
            title={profile.last_hid ? `Replay ${profile.last_hid}` : "Press a remote button first"}
            onClick={() => void action("guardian-bt-test")}
          >
            Test input
          </button>
          {connected ? (
            <button className="btn btn-outline-danger btn-sm" type="button" disabled={busy}
              onClick={() => void action("guardian-bt-disconnect")}>Disconnect</button>
          ) : (
            <button className="btn btn-primary btn-sm" type="button" disabled={busy || !inRange}
              onClick={() => void action("guardian-bt-pair")}>Connect</button>
          )}
          {!editable && (
            <button className="btn btn-outline-secondary btn-sm" type="button"
              onClick={() => setExpanded(!expanded)}>{expanded ? "Hide setup" : "Signal K setup"}</button>
          )}
        </div>
      </div>
      <div className="guardian-last-input">
        Last input: <code>{profile.last_hid || "Waiting for a button…"}</code>
      </div>
      {expanded && <MappingEditor config={config} profile={profile} onSaved={refresh} />}
    </div>
  );
}

export function GuardianBluetooth({ configuration = false }: { configuration?: boolean }): JSX.Element {
  const { config, error, refresh } = useBluetoothConfig();
  const [busy, setBusy] = useState(false);
  if (error) return <div className="alert alert-danger">Bluetooth controls unavailable: {error}</div>;
  if (!config) return <div className="guardian-loading"><span className="spinner-border spinner-border-sm" /> Loading Bluetooth devices…</div>;

  async function scan(): Promise<void> {
    setBusy(true);
    try {
      await requestButton("guardian-bt-scan");
      window.setTimeout(() => void refresh(), 500);
    } finally {
      setBusy(false);
    }
  }

  async function connectNearby(device: NearbyDevice): Promise<void> {
    setBusy(true);
    try {
      const existing = config.profiles.some((item) => item.address === device.address);
      const profiles = existing
        ? config.profiles
        : [...config.profiles, { ...device, mappings: [], last_hid: "" }];
      const next = { ...config, profiles, selected: device.address };
      await saveConfig(next);
      await requestButton("guardian-bt-pair");
      window.setTimeout(() => void refresh(), 800);
    } finally {
      setBusy(false);
    }
  }

  if (configuration) {
    return (
      <section className="guardian-bluetooth mb-4">
        <h2 className="h4">Bluetooth remotes</h2>
        <p className="text-body-secondary">Test each remote input and assign it to a custom Signal K PUT request.</p>
        <div className="guardian-device-list">
          {config.profiles.length === 0 ? <div className="guardian-empty">No paired devices yet. Pair one from Control.</div> :
            config.profiles.map((profile) => <DeviceCard key={profile.address} config={config} profile={profile} editable refresh={refresh} />)}
        </div>
      </section>
    );
  }

  const pairedAddresses = new Set(config.profiles.map((profile) => profile.address));
  const nearby = config.nearby.filter((device) => !pairedAddresses.has(device.address));
  return (
    <section className="guardian-bluetooth">
      <div className="guardian-section-heading"><h2 className="h4 mb-0">My Devices</h2></div>
      <div className="guardian-device-list mb-5">
        {config.profiles.length === 0 ? <div className="guardian-empty">No paired devices yet.</div> :
          config.profiles.map((profile) => <DeviceCard key={profile.address} config={config} profile={profile} editable={false} refresh={refresh} />)}
      </div>
      <div className="guardian-section-heading">
        <h2 className="h4 mb-0">Nearby Devices</h2>
        <button className="btn btn-outline-primary btn-sm" type="button" disabled={busy || config.scanning} onClick={() => void scan()}>
          {config.scanning ? "Scanning…" : "Scan"}
        </button>
      </div>
      <div className="guardian-device-list">
        {nearby.length === 0 ? <div className="guardian-empty">{config.scanning ? "Looking for BLE devices…" : "No new devices found. Put the QXS-001 in pairing mode and scan again."}</div> :
          nearby.map((device) => (
            <div className="guardian-device guardian-device-main" key={device.address}>
              <div className="guardian-device-icon" aria-hidden="true">⌨</div>
              <div className="guardian-device-name"><div>{device.name || "Unnamed BLE device"}</div><div className="text-body-secondary small">{device.rssi} dBm · {device.address}</div></div>
              <button className="btn btn-primary btn-sm" type="button" disabled={busy} onClick={() => void connectNearby(device)}>Connect</button>
            </div>
          ))}
      </div>
    </section>
  );
}
