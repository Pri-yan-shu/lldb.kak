declare-option line-specs lldb_flags
declare-option range-specs lldb_values

hook global BufSetOption filetype=lldb-breakpoints %{
    map buffer normal e 'xs^[^\t]+<ret>:sh-send breakpoint enable %reg{.}<ret>'
    map buffer normal E 'xs^[^\t]+<ret>:sh-send breakpoint disable %reg{.}<ret>'
    map buffer normal <a-e> 'xs^[^\t]+<ret>:sh-send breakpoint enable %reg{.}<ret>'
    map buffer normal <a-E> 'xs^[^\t]+<ret>:sh-send breakpoint disable %reg{.}<ret>'
    map buffer normal d 'xs^[^\t]+<ret>:sh-send breakpoint delete %reg{.}<ret>'
    map buffer normal i 'xs^[^\t]+<ret>:sh-send breakpoint modify --ignore %val{count} %reg{.}<ret>'
    map buffer normal a 'xs^[^\t]+<ret>:sh-send breakpoint modify --auto-continue true %reg{.}<ret>'
    map buffer normal A 'xs^[^\t]+<ret>:sh-send breakpoint modify --auto-continue false %reg{.}<ret>'
    map buffer normal o 'xs^[^\t]+<ret>:sh-send breakpoint modify --oneshot true %reg{.}<ret>'
    map buffer normal s 'xs^[^\t]+<ret>:sh-send breakpoint modify --condition '
    # map buffer normal s 'xs^[^\t]+<ret>:sh-send breakpoint modify --thread-index %val{count} %reg{.}<ret>'
    map buffer normal c 'xs^[^\t]+<ret>:sh-send breakpoint command add -o \'''' <c-r>.<a-B><c-b><c-b>'
    map buffer normal <a-c> 'xs^[^\t]+<ret>:sh-send breakpoint command add -s python -o \'''' <c-r>.<a-B><c-b><c-b>'
    map buffer normal C 'xs^[^\t]+<ret>:sh-send breakpoint command delete %reg{.}<ret>'
    map buffer normal r ':sh-send breakpoint read --file <c-x>f'
    map buffer normal w ':sh-send breakpoint write --file <c-x>f'
    map buffer normal W ':sh-send breakpoint write --append --file <c-x>f'

    add-highlighter buffer/ regex '(?S)^.+?(\t.+?)?\thit:(\d+) ign:(\d+) \(\w*\)( condition:.+)?( command:(?: .+;)+)?(?:\t([^:\n]+):(\d+):(\d+))?$' 1:function 2:value 3:value 4:variable 5:type 6:cyan 7:green 8:green

}

hook global BufSetOption filetype=lldb-watchpoints %{
    map buffer normal a ':sh-send watchpoint set '
    map buffer normal e 'xs^\d+<ret>:sh-send watchpoint enable %reg{.}<ret>'
    map buffer normal E 'xs^\d+<ret>:sh-send watchpoint disable %reg{.}<ret>'
    map buffer normal d 'xs^\d+<ret>:sh-send watchpoint delete %reg{.}<ret>'
    map buffer normal i 'xs^\d+<ret>:sh-send watchpoint ignore -i %val{count} %reg{.}<ret>'
    map buffer normal w 'xs^\d+<ret>:sh-send watchpoint modify -c  %reg{.}<ret>'
    map buffer normal c 'xs^\d+<ret>:sh-send watchpoint command add -o \'' <c-r>.<a-B><c-b><c-b>'
    map buffer normal C 'xs^\d+<ret>:sh-send watchpoint command delete %reg{.}<ret>'
    add-highlighter buffer/ regex '(?S)^\d+\t([^\s]+)\t(\d+) size:(\d+) hit:(\d+) ign:(\d+) \(\w*\)(\tcondition:.+)?$' 1:variable 2:value 3:value 4:value 5:value 6:green
}

hook global BufSetOption filetype=lldb-threads %{
    map buffer normal <ret> ':sh-send thread select %val{cursor_line}<ret>'
    map buffer normal b ':sh-send thread backtrace -c %val{count} %reg{.}<ret>'
    map buffer normal c ':sh-send thread continue %reg{.}<ret>'
    map buffer normal i ':sh-send thread info %reg{.}<ret>'
    map buffer normal r ':sh-send thread return '
    map buffer normal K ':sh-send thread siginfo %reg{.}'
    map buffer normal u ':sh-send thread until -t %reg{.} -f %val{count}<ret>'
    map buffer normal s ':sh-send thread step-over<ret>'
    map buffer normal S ':sh-send thread step-in<ret>'
    map buffer normal <a-s> ':sh-send thread step-out %val{cursor_line}<ret>'
    add-highlighter buffer/ regex '^\d+\t([^\s]+)\t([^\s]+)' 1:keyword 2:function
}

hook global BufSetOption filetype=lldb-trace %{
    map buffer normal <ret> ':sh-send kak frame_select %val{cursor_line}<ret>'
    map buffer normal u ':sh-send kak toggle_underscore_frames<ret>'
    add-highlighter buffer/ regex '(?S)^([^\t]+)\t([^\t]+)(?: \(inlined\))?(?:\t(.+):(\d+):(\d+))?$' 1:function 2:keyword 3:cyan 4:green 5:green
}

hook global BufSetOption filetype=lldb-frame %{
    map buffer normal w 'xs^\s*\K[^\s]+<ret>:sh-send watchpoint set variable %reg{.}<ret>'
    map buffer normal p 'xs^\s*\K[^\s]+<ret>:sh-send kak show_value %reg{.} %val{count}<ret>'
    map buffer normal s 'xs^\s*\K[^\s]+<ret>:sh-send expr <c-r>. = '
    map buffer normal d ':sh-send kak var_depth %val{count}<ret>'
    map buffer normal [ '' # array from ptr

    add-highlighter buffer/ regex ^(.+?):(.+?)(?:=(.+?))?$ 1:variable 2:type 3:value
}

define-command -params 1 lldb-buffer %{
    edit "%opt{sh_fifo}/%arg{1}"
    set-option buffer filetype "lldb-%arg{1}"
    set-option buffer autoreload yes
    set-option buffer fs_check_timeout 100
}

define-command -params 1.. lldb-spawn %{
    sh-spawn %arg{@}
	rename-buffer lldb
	set-option global sh_fifo %opt{sh_fifo}

	lldb-buffer breakpoints
    add-highlighter buffer/ wrap -word -marker '    '
	lldb-buffer watchpoints
	lldb-buffer threads
	lldb-buffer trace
	lldb-buffer frame

	add-highlighter global/lldb-flags flag-lines red lldb_flags
}

define-command lldb-despawn %{
    try %{db! "%opt{sh_fifo}/breakpoints"}
    try %{db! "%opt{sh_fifo}/watchpoints"}
    try %{db! "%opt{sh_fifo}/threads"}
    try %{db! "%opt{sh_fifo}/trace"}
    try %{db! "%opt{sh_fifo}/frame"}
    try %{db! "lldb"}

    remove-highlighter global/lldb-flags
    evaluate-commands -buffer * %{ unset-option buffer lldb_flags }
}

declare-user-mode lldb
map global lldb a ':sh-send process attach -p '
map global lldb A ':sh-send process attach -n '
map global lldb b ':sh-send breakpoint set --file %val{bufname} --line %val{cursor_line}<ret>'
map global lldb B ':sh-send breakpoint set --file %val{bufname} --line %val{cursor_line} --condition ''''<left>'
map global lldb c ':sh-send continue<ret>'
map global lldb d ':sh-send process detach<ret>'
map global lldb e ':sh-send expr %reg{.}<ret>'
map global lldb E ':sh-send settings remove target.env-vars '
map global lldb i ':sh-send settings set target.process.thread.step-avoid-regexp '
map global lldb I ':sh-send settings show target.process.thread.step-avoid-regexp<ret>'
map global lldb j ':sh-send kak jump<ret>'
map global lldb k ':sh-send process kill<ret>'
map global lldb K ':sh-send process signal '
map global lldb l ':sh-send process launch<ret>'
map global lldb L ':sh-send process launch --tty=/dev/pts/'
map global lldb q ':lldb-spawn lldb<ret>'
map global lldb Q ':lldb-despawn<ret>'
map global lldb s ':sh-send thread step-over<ret>'
map global lldb S ':sh-send thread step-in<ret>'
map global lldb <a-s> ':sh-send thread step-out<ret>'
map global lldb t ':sh-send target create <c-x>f'
map global lldb w '<a-i>w:sh-send watchpoint set variable %reg{.}<ret>'
map global lldb W ':sh-send watchpoint set expression %reg{.}<ret>'
# map global lldb <a-w> ':sh-send watchpoint modify -c ''(%reg{.})''<left><left>'

declare-user-mode lldb-hook
map global lldb h ':enter-user-mode lldb-hook<ret>'
map global lldb-hook l ':sh-send target stop-hook list<ret>'
map global lldb-hook a ':sh-send target stop-hook add '
map global lldb-hook e ':sh-send target stop-hook enable '
map global lldb-hook d ':sh-send target stop-hook disable '
map global lldb-hook D ':sh-send target stop-hook delete '
