# Compatibility

Solar's application IDL, normalized IR, lock file, manifest, shipment, and
Python package formats are independently versioned. The compiler rejects
identity reuse, incompatible field changes, unbounded firmware values, and
breaking evolution unless deliberately accepted.

The standard Python SDK can bind exactly, by compatible interface, or by build
policy. Generated application clients validate the effective linked manifest;
they do not trust authored YAML alone.
