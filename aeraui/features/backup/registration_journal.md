# Package registration recovery

Registration commits stage replacement and original payloads in a private
`.aera-registration` directory beside Android package state. Payloads and the
manifest are synced before publishing `active`; each replacement and target
parent are synced before the journal is retired. Retained payloads make replay
repeatable even after some target renames have completed. Per-file SHA-256,
metadata, target whitelists and original/intended generation checks reject
corrupted journals, symlinks and externally changed Android package state.

AERA replays a pending commit before PackageDatabase, Users and
RegisteredApkDirectories read package state. Commit errors leave a redo journal
rather than claiming the original database was rolled back. Return to AERA
App Vault to complete registration before booting Android.

Android does not consume this journal. App Vault therefore arms the persistent
[boot guard](bootguard.md) before APK/data extraction and keeps Android boot
blocked until the matching restore and registration are durable. Unverified
persistent recovery installations refuse restore before writing app files.
The journal does not make preceding APK/data extraction or ownership repairs
atomic. Interrupted staging or retired-journal cleanup can leave private
payload directories consuming disk space; they are not active transactions and
are not deleted automatically as unknown paths. Completed normal commits clean
their own payloads. Fault-injection tests exercise actual filesystem operations,
not real power-loss behavior of a device storage controller.
