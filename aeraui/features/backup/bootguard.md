# AppVault persistent boot guard

A restore arms a durable `boot-recovery` BCB marker before Restic writes app
files. The marker carries a 64-character canonical transaction SHA-256 and a
Writing or Registration phase. Writing is never automatically cleared: APK or
data writes may have stopped midway and must be explicitly retried with the same
identity. A different restore cannot clear it. Registration can be completed only
after its matching redo journal and restored data have been durably synchronized.
Startup journal replay alone must not clear Writing or infer restore completion.

Both Alioth boot slots must contain a header-v3 recovery ramdisk whose recovery
executable exactly matches the running guard-capable executable. Proof reads boot
partitions without writing them, stages snapshots only in private RAM directories,
and extracts only the recovery executable with the bundled root-owned unpacker.
This conservatively refuses a temporary fastboot boot when persistent recovery is
stock, older, or unverifiable, and refuses a one-slot installation that could fall
back to an incompatible recovery. It does not install recovery or change slots.

BCB clear paths must preserve recognized and malformed markers, and system reboot
must fail closed on pending guards or BCB read errors. External bootloader actions,
manual slot/image changes, or external software overwriting misc can bypass this
protocol; executable identity is not a proof of bootloader behavior. File-backed
fault tests establish the shared phase/identity protocol at fsync boundaries,
not physical storage atomicity during a torn misc-sector write.

System reboot intent is latched under the guard mutex before any unmount or reboot
work. A simultaneous restore therefore either arms first and blocks reboot, or
refuses to start after reboot intent. If a system reboot fails, restart recovery
before attempting another restore. Package database access replays the redo
journal after decrypted data is available; replay does not release the boot guard.
