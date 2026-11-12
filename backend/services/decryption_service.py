"""Archive decryption stub.

Replace the body with the real algorithm (e.g. AES-GCM from the cryptography
package) once the encryption side is implemented on the device.
"""


def decrypt_blob(data: bytes, key: bytes = b"") -> bytes:
    return data


def is_encrypted(name: str) -> bool:
    return name.endswith(".enc")