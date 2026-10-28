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

### Needs Further Investigation

- [ ] ability to resize font in all the application -> which panel, all? separately?

### Wish list

- [ ] add a theme manager
- [ ] detect automatically file type for syntax color instead of looking for its file extension (use magic?). This may degrade performace.
- [x] add a static build: doesn't work on debian
- [ ] add jupyter notebook preview support
- [ ] add office files preview (word, excel, libreoffice)
- [ ] create a minimal configure script
- [ ] mvp for a modern look
- [ ] enhance markdown rendering
- [ ] integrate test suite
- [ ] zoom in preview for pdf and image
- [ ] use three modes in the editor: vim, emacs, notepad (default)
- [ ] in the explorer panel, add a mini icon that display remote (SSH) machines user can connect to
- [ ] change files icon to match vscode (.c, .h, .md files)

## Bugs

- [ ] git status in explorer panel for modified files is not up to date, or at least there is a huge delay
- [x] files opened with editor are seen as modified while they're just opened untouched
- [ ] cannot select text near the panel edge (it grabs the edge to resize it)