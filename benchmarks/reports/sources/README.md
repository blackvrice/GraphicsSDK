# Measured SDK source snapshots

Archive names are verified SDK build fingerprints. These contain own SDK code only.
Build the archive with `-DGFX_BUILD_TESTS=OFF -DGFX_BUILD_EXAMPLES=OFF`; tests/examples and competitor source are separate.
Pins and raw observations remain in the parent project. OPT-001 before was reconstructed by reverting exactly its ready() change and verified against the recorded fingerprint.
