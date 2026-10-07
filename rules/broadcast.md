# Split terminal input

- Host aliases (`WookAlias`) and RGB colors (`WookTabColor`) are display metadata; they must not change SSH endpoints, credential scopes or storage identities. Preserve them in settings backups.
- The command bar and live keyboard synchronization target only currently visible split terminals. Both checkboxes start unchecked and reset when the pane group changes or the workspace returns home/single view.
- Exclude SFTP, preview, closed and unauthenticated sessions. Windows checks the backend's input-ready state. Mac uses OpenSSH's post-authentication LocalCommand marker, never remote prompt text.
- Broadcast only deliberate user input, including committed IME text, keys and paste. Do not replicate terminal replies, remote output, composition previews or mouse coordinates. Received broadcast input must not produce another broadcast.
- Keep typed commands in memory only; do not save history or log payloads. Display the selected mode and command delivery/skipped counts.
- Validate with isolated local terminals: active-only delivery, all-pane delivery, exact input, hidden-tab exclusion and resetting modes after pane changes. Preserve existing SSH, SFTP and IME regression tests.
- Windows selected targets are tied to pane objects, not tab indices. Explicitly excluded panes neither send nor receive live broadcasts. Target selection resets with the pane group, home, reconnect and zoom/restore.
- Translate navigation/editing keys using each destination terminal's existing PuTTY keyboard translator and mode. Preserve and restore its thread keyboard state; suppress both raw-input duplication and rebroadcast. Scrollback and application shortcuts remain local.
- Validate non-cancelling cursor edits in local CMD and exact key streams through independent SSH sessions with different cursor modes. Assert both hidden and excluded destinations remain untouched.
