import ed25519
import base64

private_key, public_key = ed25519.create_keypair()
message = b"Ok to open that door."

print(base64.b64encode(private_key.to_seed()).decode('ASCII'))
print(base64.b64encode(public_key.to_bytes()).decode('ASCII'))
print(base64.b64encode(private_key.sign(message)).decode('ASCII'))

