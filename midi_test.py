import mido

print("Available MIDI inputs:")
inputs = mido.get_input_names()
for port in inputs:
    print(f" - {port}")

helix_port = None
for port in inputs:
    if "Helix" in port:
        helix_port = port
        break

if helix_port is None:
    print("\nCould not find 'Helix' in available MIDI inputs.")
else:
    print(f"\nConnecting to: {helix_port}")
    print("Listening for messages... (Press Ctrl+C to stop)")
    try:
        with mido.open_input(helix_port) as port:
            for msg in port:
                if msg.type != 'clock':
                    print(msg)
    except KeyboardInterrupt:
        print("\nStopped listening.")
