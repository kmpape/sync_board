"""Compatibility import for the archived scratch controller.

The live framed serial implementation is maintained in
``syncboard.serialconnection`` so this copy cannot diverge again.
"""

from syncboard.serialconnection import (
    SerialConnection,
    SerialProtocolError,
    SerialResponseTimeout,
)

__all__ = ["SerialConnection", "SerialProtocolError", "SerialResponseTimeout"]
