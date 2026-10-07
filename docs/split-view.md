# Split view

Open at least two tabs, select a terminal, and choose **Split**:

| Layout | Arrangement |
| --- | --- |
| Single pane | Selected tab fills the workspace |
| 2 panes | Side by side |
| 3 panes | Full-height left pane, two stacked right panes |
| 4 panes | 2 × 2 grid |

The initial group contains the selected tab followed by existing tabs in tab order. Open more tabs to enable larger layouts. Choosing a layout does not create new connections or terminate any sessions.

Click a terminal or its pane heading to focus it. The selected tab and heading identify the active pane; typing, duplicate, reconnect and close commands apply to that tab by default. Selecting a tab already visible focuses its existing pane. Selecting a hidden tab replaces only the active pane; the replaced session continues running in its tab.

**Single pane** restores the ordinary tab view. **Workspace/Home** temporarily hides the group. Closing a tab removes its pane and fills the vacancy from another existing tab when available. Tab reordering preserves pane identities. Resizing the window updates terminal rows/columns for each visible pane.

Windows: toolbar **Split**, or `Ctrl+Shift+S`. macOS: toolbar **Split**, **Tabs → Split layout**, or `⌘⇧S`. The iPad preview has a **Split** menu beside its tabs; Windows and Mac are the initial release priority.

Layouts have fixed proportions; draggable dividers and persisted layouts are not implemented. SFTP tabs can be displayed alongside terminals, but wide file controls are most comfortable in larger panes or single-pane mode.

## Host alias and color (Windows and Mac)

Edit a saved host and set **Alias** and **Tab color**. An empty alias uses the saved host name. The alias labels tabs and pane headings; a color stripe identifies every tab, with corresponding split-heading colors. Windows also colors each pane border. Host identity colors do not change the terminal's ANSI palette. Aliases and colors are included in settings export/import.

## Common command bar (Windows and Mac)

In split view, enter one command in the bottom field and press **Enter** or **Send**. It sends the text followed by Enter to the active terminal. Check **Send to all panes** to send it to all visible connected terminal panes. The status reports delivery and skipped panes. Hidden tabs, SFTP, preview and unauthenticated sessions are excluded. Commands are not stored as history.

Check **Sync keyboard** to mirror input from the active terminal: typing, navigation keys, Backspace, Ctrl+C and paste. Only committed IME text is mirrored. Mouse actions, scrollback, app shortcuts, remote output and terminal protocol replies are not mirrored. Ordinary input remains independent when unchecked; this option is separate from **Send to all panes**.

Both checkboxes start off. Changing the pane group, returning home or switching to single-pane view turns them off. Review the visible aliases/colors before enabling either mode. The same input goes to each terminal; different shells, applications and working directories can interpret it differently. Passwords typed into remote programs will also be mirrored while **Sync keyboard** is enabled.

![macOS split terminals](../assets/screenshots/mac-split.png)
