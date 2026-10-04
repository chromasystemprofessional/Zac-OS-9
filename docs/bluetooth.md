# Bluetooth

The **Bluetooth** control panel (`zacos9-bluetooth`, `shell/panels/bluetooth.cpp`)
pairs and connects Bluetooth devices, and sends sound to headphones and speakers.
It is in Apple menu → Control Panels.

## The window

- **Bluetooth On** turns the adapter on and off (BlueZ's `Adapter1.Powered`).
- **Devices** lists connected devices first, then paired ones, then devices a
  search has found. Each line ends in a status:
  - Connected, sound plays here
  - Connected
  - Paired
  - Found
  - Connecting…

  Devices nearby that send no name and aren't paired are left out.
- **Search** starts BlueZ discovery. It stops by itself after a minute, or
  when **Stop Searching** is clicked or the panel closes.
- **Connect** (or a double-click on a device):
  1. If the device isn't paired yet, marks it trusted, so it can reconnect by
     itself later, then pairs with it.
  2. Connects it.
  3. For a headset or speaker (A2DP sink, headset or hands-free profile),
     waits up to about 10 seconds for PipeWire's `bluez_output.<address>…`
     sink, then makes it the default output with `pactl set-default-sink`.

  On a connected device, the button says **Disconnect**.
- **Play Sound Here** makes a connected audio device the sound output again,
  for example after choosing another output.
- **Forget** removes the pairing (`Adapter1.RemoveDevice`).

## How it works

- **Talking to BlueZ.** The panel uses BlueZ (`org.bluez`) on the system
  D-Bus.
  - It reads the device list with `ObjectManager.GetManagedObjects`. That is
    one local call, made every 3 seconds, or every second while searching or
    connecting.
  - Everything else is asynchronous, so the window keeps drawing.
- **Pairing agent.** While the panel is open, it registers a pairing agent
  with "NoInputNoOutput" capability at `/org/zacos9/bluetooth/agent`. BlueZ
  then pairs headphones and speakers "just works", without asking for a PIN.
  If an older device asks anyway, it is given `0000`.
- **Sound.** The output goes through `pactl`, as in the Sound panel. When a
  device disconnects, PipeWire goes back to the previous output by itself.
- **Package.** `debian/control` depends on `bluez`. PipeWire's Bluetooth
  plugin (`libspa-0.2-bluetooth`) comes with Debian's PipeWire.

## Not done

- **Keyboards and phones.** Devices that need a passkey typed or compared
  pair, but the passkey isn't shown.
- **Paired devices with the panel closed.** They reconnect by themselves when
  they are trusted, but if one asks to pair again, nothing answers.
- **Device icons.** The list shows text only.
- **Sound panel.** It doesn't list outputs yet. The Bluetooth panel is where
  to choose a Bluetooth output.
