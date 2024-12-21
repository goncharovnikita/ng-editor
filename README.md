# ng-editor

> recreation of vim in c

## TODO

- [ ] User input handling
- - [ ] Handle ^C - alert user to use :qa!
- Navigation
- - By-word navigation
- - - [x] `w` - jump to beginning of next word
- - - [x] `b` - jump to beginning of prev word
- - - [x] `e` - jump to end of current word, or next word if already at end
- - Buffer navigation
- - - [x] `h` - navigate one position backwards
- - - [x] `j` - navigate one position down
- - - [x] `k` - navigate one position up
- - - [x] `l` - navigate one position forward
- - - [x] `H` - navigate to top of current buffer
- - - [x] `M` - navigate to middle of current buffer
- - - [x] `L` - navigate to bottom of current buffer
- - - [x] `G` - navigate to bottom of current source file
- - - [x] `gg` - navigate to top of current source file
- - - [x] `^D` - scroll down by half of buffer size
- - - [x] `^U` - scroll up by half of buffer size
