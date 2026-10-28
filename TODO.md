# TODO

## Features

### Must do

- [x] implement zt/zb shortcut in the file panel to move cursor view on top/bottom of the panel
- [x] files are not added/deleted in subfolders of the file panel
- [x] add command line option to start with a specific size (eg --size 1920x1080)
- [x] the file panel is now called the explorer panel. On the explorer panel, there is 2 views: files or bookmarks, but not both at the same time
- [x] dont resize the file panel when the help panel is activated
- [x] editor tabs must stay on their workspace
- [x] BUG detect git changes and display the files status accordingly in the explorer panel
- [x] add preview for video files
- [x] add preview for audio files
- [x] add expand/collapse buttons in the explorer panel
- [x] somehow highlight the panel that owns the focus
- [x] ability to move editor tabs in a dedicated panel, right to the preview panel
- [x] add minimap in editor
- [x] for git repo, add a git tag right to the preview panel that list commit
- [x] remote mode: in the left side panel, add a remote (SSH) mode alognside the explorer/bookmarks/notes, that display a list of machines user can connect to, to browse files and preview and edit them remotely in the explorer panel. When connected to a remote machine, the terminal button must open SSH to the remote machine.
- [x] add notes type functionnality: a note can be of the 3 types: text, rich format (that supports rich text, images, etc), and orgmode (implement display and behaviour like in emacs orgmode).
- [ ] OS support: Windows/MacOS support, maybe using meson buld system
- [ ] add a theme manager

### Preview

- [ ] add jupyter notebook preview support
- [ ] add office files preview (word, excel, libreoffice)
- [ ] add support for certificate preview

### Needs Further Investigation

- [ ] ability to resize font in all the application -> which panel, all? separately?
- [x] vim mode: use three modes in the editor: vim, emacs, notepad (default)

### Wish list

- [ ] detect automatically file type for syntax color instead of looking for its file extension (use magic?). This may degrade performace.
- [x] add a static build: doesn't work on debian
- [ ] mvp for a modern look
- [ ] enhance markdown rendering
- [ ] zoom in preview for pdf and image
- [ ] change files icon to match vscode (.c, .h, .md files)
- [ ] don't raise external window to get input, every interaction must reside inside the application (strong convention for this project)
- [ ] view which lines are modified regarding git diff in the preview and editor, like in vscode with the green vertical bar
- [ ] provide appimage
- [ ] implement a music player: configure the music library (a folder where user stores its music) then analyse the folder and list songs to play
- [ ] add configuration panel
- [ ] add a search engine in the left side panel
- [ ] implement wrap line option in editors

## Bugs

- [x] DONE, TO BE CONFIRMED - git status in explorer panel for modified files is not up to date, or at least there is a huge delay
- [x] files opened with editor are seen as modified while they're just opened untouched
- [ ] cannot select text near the panel edge (it grabs the edge to resize it)
- [ ] when clicking on the git commits tab, refresh the list
- [ ] in the left side panel, in the explorer file tree sometime the git status indicators are troncating the file name while is a lot of space on the right to put the status. It's also the case on the notes panel to indicate how many notes in the notebooks, and also in the bookmark panel so maybe on all panels.
- [ ] sliders in editor panel can overlap text