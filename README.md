# refpad
refpad is a reference Notepad-like Unicode text editor. It is meant to explore the problem space of editing and displaying multi-lingual Unicode text and present simple solutions through the implementation of a textbox-in-a-window editor. It is also used as an example project to showcase version 2 of the kb_text_shape API.

# Functionality
refpad does not try to handle large amounts of text: there are hard limits for total text length and line count, and the entirety of the text is shapen and laid out every frame.

refpad supports multilingual and multi-style text, including mixed left-to-right and right-to-left text. A hardcoded list of fonts, which are included in this repository, is loaded on startup, and kb_text_shape is responsible for choosing the appropriate font to display each part of the text. Selecting and loading system fonts is out of scope for this project.

refpad supports a number of standard text editor actions:
- `Escape`: exit the program
- `Left`: move to previous grapheme
- `Right`: move to next grapheme
- `Up`: move to previous line
- `Down`: move to next line
- `Ctrl+Left`: move to previous word
- `Ctrl+Right`: move to next word
- `Ctrl+Up`: move to previous paragraph
- `Ctrl+Down`: move to next paragraph
- `Home`: move to the beginning of the line
- `End`: move to the end of the line
- `PageUp`: move up the height of a screen
- `PageDown`: move down the height of a screen
- `Shift`: hold while moving to select
- `Backspace`: delete one codepoint before the cursor
- `Delete`: delete one grapheme after the cursor
- `Ctrl+Backspace`: delete one word before the cursor
- `Ctrl+Delete`: delete one word after the cursor
- `Ctrl+A`: select all
- `Ctrl+C`: copy to clipboard
- `Ctrl+V`: paste from clipboard
- `Ctrl+X`: cut to clipboard
- `Ctrl+Z`: undo
- `Ctrl+Y`: redo
- `Ctrl+Plus`: increase font size
- `Ctrl+Minus`: decrease font size

refpad also has several actions meant for exploring text editor functionality:
- `Ctrl+R`: toggle display of newline characters
- `Ctrl+W`: toggle line wrapping
- `Ctrl+B`: toggle bold style on selection
- `Ctrl+I`: toggle italic style on selection

# Dependencies
refpad uses SDL3. This repository contains pre-built binaries of SDL3 for Windows. On Linux, you will need to have installed the development version of the library in order to compile the editor.

Furthermore, refpad uses the stb_truetype and kb_text_shape single-header libraries.

SDL3 and stb_truetype are used to open a window and render text on Windows and Linux. They could, in theory, be replaced with other backends. kb_text_shape, on the other hand, is used in the core of the editor.

# Compiling
On Windows: `build.bat`

On Linux: `sh build.sh`