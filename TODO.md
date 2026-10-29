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

Planned in [TASK.md](TASK.md): rules, roadmap and definition of done for every preview format.

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
- [x] don't raise external window to get input, every interaction must reside inside the application (add this convention for the project)
- [ ] view which lines are modified regarding git diff in the preview and editor, like in vscode with the green vertical bar
- [ ] provide appimage
- [ ] implement a music player: configure the music library (a list of folders where user stores its music) then analyse the folders and list songs to play
- [ ] implement a video player: configure the video library (a list of folders where user stores its videos) then analyse the folders and list video to play
- [ ] add configuration panel
- [ ] add a search engine in the left side panel
- [ ] implement wrap line option in editors
- [ ] mouse whell scroll in the minimap
- [ ] search engine
- [ ] add container explorer: list containers, connect and list files, open terminal
- [x] rework the focus highlight, make it more subtile than just highlight the frame of the panel
- [x] when creating a new workspace, use a default view in the new workspace instead of cloning the current one
- [/] add the ability to save workspace layouts (panel szes and placement)
- [ ] ability to change window size from the application, to default sizes: 1920x1080, 1600x1200, etc
- [ ] add a systemd services explorer. List services, show status and logs in preview when selecting the service. Must work for remote machines
- [ ] rewrite the README. What are the important features? The problems this application solve?
- [ ] git status can be moved to a dedicated panel. User can checkout commits (Enter key) to browse files in the explorer file panel
- [ ] add lines number for code files
- [ ] what about -O3 and -Werror in CFLAGS?

### README

- [ ] dev env for agent tests: sudo apt install xvfb xdotool imagemagick

## Bugs

- [x] DONE, TO BE CONFIRMED - git status in explorer panel for modified files is not up to date, or at least there is a huge delay
- [x] files opened with editor are seen as modified while they're just opened untouched
- [x] I cannot select text near the panel edge (it grabs the edge to resize it)
- [x] in the left side panel, in the explorer file tree sometime the git status indicators are troncating the file name while there is a lot of space on the right to put the status. It's also the case on the notes panel to indicate how many notes in the notebooks, and also in the bookmark panel so maybe on all panels. I see it happens when I cycle on the veiws file, bookmarks, notes, etc.
- [ ] sliders in editor panel can overlap text
- [x] git commits tab is not updated when a commit is performed