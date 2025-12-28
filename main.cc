#include "lldb/lldb-enumerations.h"
#include <lldb/API/LLDB.h>
#include <climits>
#include <cstdint>
#include <cstring>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

using namespace lldb;

bool running = true;
SBDebugger *debugger = nullptr;
std::unordered_map<uint16_t, const char *> line_flags;

const char *pwd;
char kak_cmd_string[64];
char path_breakpoints[64];
char path_watchpoints[64];
char path_threads[64];
char path_frames[64];
char path_frame[64];

bool trace_all_threads = false;
uint8_t trace_max_depth = 0;
uint8_t frame_max_depth = 1;
uint16_t jump_line = 0;

const char *relpath(const char *path) {
    size_t pwd_len = strlen(pwd);
    if (strncmp(pwd, path, pwd_len) == 0) {
        return &path[pwd_len + 1];
    } else {
        return path;
    }
}

void kak_jump(SBLineEntry line_entry) {
    FILE* cmd = popen(kak_cmd_string, "w");
    SBFileSpec spec = line_entry.GetFileSpec();

    if (!cmd)
        return;

    if (!spec.Exists()) {
        fputs("fail testing besting best\n", cmd);
        pclose(cmd);
        return;
    }

    char path[255];
    spec.GetPath(path, 255);

    const char *rp = relpath(path);

    jump_line = line_entry.GetLine();

    fprintf(cmd, "eval -try-client client0 %%{e %s %u %u; addhl -override buffer/lldb-line line %d ,rgb:333333}\n",
            path, jump_line, line_entry.GetColumn(), jump_line);

    // fprintf(cmd, "addhl -override buffer/lldb-line line %d bright-black\n", jump_line);
    // fprintf(cmd, "addhl -override buffer=%s/lldb-line line %d bright-black\n", rp, jump_line);
    fprintf(stderr, "highlighting: %s:%d\n", rp, jump_line);
    pclose(cmd);
}

void breakpoints_line_flags(SBEvent event) {
    BreakpointEventType type = SBBreakpoint::GetBreakpointEventTypeFromEvent(event); 
    SBBreakpoint bp = SBBreakpoint::GetBreakpointFromEvent(event);

    bool remove = false;
    const char *add_color = NULL;

    switch (type) {
    case eBreakpointEventTypeRemoved:
    case eBreakpointEventTypeLocationsRemoved:
        remove = true;
        break;
    case eBreakpointEventTypeDisabled:
        remove = true;
        add_color = "blue";
        break;
    case eBreakpointEventTypeAdded:
    case eBreakpointEventTypeLocationsAdded:
    case eBreakpointEventTypeLocationsResolved:
        add_color = "red";
        break;
    case eBreakpointEventTypeEnabled:
        add_color = "red";
        remove = true;
        break;
    case eBreakpointEventTypeCommandChanged:
    case eBreakpointEventTypeConditionChanged:
    case eBreakpointEventTypeThreadChanged:
    case eBreakpointEventTypeAutoContinueChanged:
    case eBreakpointEventTypeInvalidType:
    case eBreakpointEventTypeIgnoreChanged:
    default:
        break;
    }

    FILE* cmd = popen(kak_cmd_string, "w");
    if (!cmd)
        return;
    // for (int i = 0; i < SBBreakpoint::GetNumBreakpointLocationsFromEvent(event); i++) {
        // SBBreakpointLocation loc = SBBreakpoint::GetBreakpointLocationAtIndexFromEvent(event, i); 
    for (int i = 0; i < bp.GetNumLocations(); i++) {
        SBBreakpointLocation loc = bp.GetLocationAtIndex(i); 
        SBLineEntry line = loc.GetAddress().GetLineEntry();
        SBFileSpec file = line.GetFileSpec();

        const char *filename = file.GetFilename();
        const char *dir = file.GetDirectory();
        const int line_no = line.GetLine();

        char path[255];
        file.GetPath(path, 255);
        const char *rp = relpath(path);

        if (remove) {
            fprintf(cmd, "set-option -remove buffer=%s lldb_flags %d|{%s}●\n", rp, line_no, line_flags.at(line_no));
            line_flags.erase(line_no);
        }

        if (add_color) {
            line_flags[line_no] = add_color;
            fprintf(cmd, "set-option -add buffer=%s lldb_flags %d|{%s}●\n", rp, line_no, add_color);
        }
    }

    pclose(cmd);
}

void write_breakpoints() {
    FILE *file = fopen(path_breakpoints, "w");
    SBTarget target = ::debugger->GetSelectedTarget();
    char path[256];

    for (int i = 0; i < target.GetNumBreakpoints(); i++) {
        SBBreakpoint bp = target.GetBreakpointAtIndex(i);
        const char *condition = bp.GetCondition();

        for (int j = 0; j < bp.GetNumLocations(); j++) {
            SBBreakpointLocation location = bp.GetLocationAtIndex(j); 
            SBAddress address = location.GetAddress();
            SBLineEntry line = address.GetLineEntry();
            line.GetFileSpec().GetPath(path, 256);

            fprintf(file, "%d.%d %s hit:%d ignore:%d%s%s\"%s\" %s:%d:%d\n", bp.GetID(), j+1,
                address.GetFunction().GetDisplayName(), bp.GetHitCount(),
                bp.GetIgnoreCount(), bp.IsOneShot() ? " oneshot" : "",
                condition ? " condition: " : "", condition ? condition : "",
                path, line.GetLine(), line.GetColumn());
        }
    }

    fclose(file);
}

void write_watchpoints() {
    FILE *file = fopen(path_watchpoints, "w");

    SBTarget target = ::debugger->GetSelectedTarget();

    for (int i = 0; i < target.GetNumWatchpoints(); i++) {
        SBWatchpoint wp = target.GetWatchpointAtIndex(i); 

        const char *condition = wp.GetCondition();

        fprintf(file, "%d %s%s %lu size:%lu hit:%d ignore:%d%s%s %s%s\n", wp.GetID(), wp.GetWatchSpec(),wp.IsEnabled() ? "" : " (disabled)",
                wp.GetWatchAddress(), wp.GetWatchSize(), wp.GetHitCount(),
                wp.GetIgnoreCount(), condition ? " condition: " : "", condition ? condition : "", 
                wp.IsWatchingReads() ? "r" : "", wp.IsWatchingWrites() ? "w" : "");

    }
    fclose(file);
}

void write_threads(SBProcess process) {
    FILE *file = fopen(path_threads, "w");

    for (int i = 0; i < process.GetNumThreads(); i++) {
        SBThread thread = process.GetThreadAtIndex(i);

        SBValue retval = thread.GetStopReturnValue();
        StopReason stop_reason = thread.GetStopReason();
        char stop_desc[256] = "";
        if (thread.IsStopped())
            thread.GetStopDescription(stop_desc, 256); 

        fprintf(file, "%lu %s %d %s %s %s%s\n", thread.GetThreadID(), thread.GetName(),
                thread.GetNumFrames(), thread.GetSelectedFrame().GetDisplayFunctionName(),
                retval.IsValid() ? retval.GetValue() : "",
                stop_desc, thread.IsSuspended() ? " S" : "");

    }
    fclose(file);
}

void write_trace(SBThread thread) {
    FILE *file = fopen(path_frames, "w");

    SBValue return_value = thread.GetStopReturnValue();
    const char *ret = "";

    if (return_value.IsValid()) {
        ret = return_value.GetValue();
    }

    uint32_t num_frames = thread.GetNumFrames();

    if (trace_max_depth && trace_max_depth > num_frames) {
        num_frames = trace_max_depth;
    }

    for (int i = 0; i < num_frames; i++) {
        SBFrame frame = thread.GetFrameAtIndex(i); 
        SBLineEntry line_entry = frame.GetLineEntry();
        char plc[256] = "";

        if (line_entry.IsValid()) {
            char path[256];
            line_entry.GetFileSpec().GetPath(path,256);
            snprintf(plc, 256, "%s:%d:%d", path, line_entry.GetLine(), line_entry.GetColumn());
        }

        // fprintf(file, "%c %s\t%s\t%s%s\n", frame == thread.GetSelectedFrame() ? '>' : ' ',
        fprintf(file, "%s\t%s\t%s%s\n",
                frame.GetFunctionName(), frame.GetModule().GetFileSpec().GetFilename(),
                plc, frame.IsInlined() ? "\ti" : "");

    }
    fclose(file);
}

void write_value(FILE *file, SBValue val, uint32_t max_depth, uint32_t depth = 0) {
    SBStream path;
    const char *expr_path = val.GetExpressionPath(path) ? path.GetData() : val.GetName();
    fprintf(file, "%s\t: %s", expr_path, val.GetTypeName());

    const char *summary = val.GetSummary();

    if (summary == nullptr)
        summary = val.GetValue();

    if (summary != nullptr)
        fprintf(file, "\t= %s", summary);

    fprintf(file, "\n");

    const uint32_t num_children = val.GetNumChildren();
    if (depth >= max_depth || num_children == 0 || num_children > 16) {
        return;
    }

    for (int i = 0; i < num_children; i++) {
        SBValue child = val.GetChildAtIndex(i);
        for (int j = 0; j <= depth; j++) 
            fprintf(file, "\t");
        write_value(file, child, max_depth, depth + 1);
    }
}

void write_frame(SBFrame frame) {
    FILE *file = fopen(path_frame, "w");

    frame.GetSymbolContext(3).GetFunction().GetInstructions(frame.GetThread().GetProcess().GetTarget()); 

    SBValueList vals = frame.GetVariables(true, true, false, false);
    for (int i = 0; i < vals.GetSize(); i++) {
        SBValue val = vals.GetValueAtIndex(i); 
        write_value(file, val, frame_max_depth);
    }
    fclose(file);
}

void handle_event(SBEvent event) {
    if (SBBreakpoint::EventIsBreakpointEvent(event)) {
        BreakpointEventType type = SBBreakpoint::GetBreakpointEventTypeFromEvent(event); 
        SBBreakpoint bp = SBBreakpoint::GetBreakpointFromEvent(event);

        switch (type) {
        case eBreakpointEventTypeAdded:
        case eBreakpointEventTypeRemoved:
            break;
        case eBreakpointEventTypeEnabled:
        case eBreakpointEventTypeDisabled:
        case eBreakpointEventTypeLocationsAdded:
        case eBreakpointEventTypeLocationsRemoved:
        case eBreakpointEventTypeLocationsResolved:
            breakpoints_line_flags(event); 
        case eBreakpointEventTypeIgnoreChanged:
        case eBreakpointEventTypeAutoContinueChanged:
        case eBreakpointEventTypeCommandChanged:
        case eBreakpointEventTypeConditionChanged:
            write_breakpoints();
            break;
        case eBreakpointEventTypeThreadChanged:
        case eBreakpointEventTypeInvalidType:
            break;
        }
    } else if (SBWatchpoint::EventIsWatchpointEvent(event)) {
        WatchpointEventType type = SBWatchpoint::GetWatchpointEventTypeFromEvent(event); 
        SBWatchpoint bp = SBWatchpoint::GetWatchpointFromEvent(event);

        switch (type) {
            case eWatchpointEventTypeAdded:
            case eWatchpointEventTypeRemoved:
            case eWatchpointEventTypeEnabled:
            case eWatchpointEventTypeDisabled:
            case eWatchpointEventTypeCommandChanged:
            case eWatchpointEventTypeConditionChanged:
            case eWatchpointEventTypeIgnoreChanged:
            case eWatchpointEventTypeTypeChanged:
                write_watchpoints();
                break;
            case eWatchpointEventTypeThreadChanged:
            case eWatchpointEventTypeInvalidType:
                break;
        }
    } else if (SBThread::EventIsThreadEvent(event)) {
        uint32_t mask = event.GetType();
        bool do_write_thread = false;
        bool do_write_trace = false;
        bool do_write_frame = false;

        if (mask & SBThread::eBroadcastBitStackChanged) {
            do_write_trace = true;
            do_write_thread = true;
        }

        if (mask & SBThread::eBroadcastBitThreadSelected) {
            do_write_trace = true;
        }

        if (mask & SBThread::eBroadcastBitSelectedFrameChanged) {
            do_write_frame = true;
        }

        if (mask & SBThread::eBroadcastBitThreadSuspended
                   || mask & SBThread::eBroadcastBitThreadResumed) {
            do_write_thread = true;
        }

        SBThread thread = SBThread::GetThreadFromEvent(event);
        SBProcess process = thread.GetProcess();
        if (do_write_thread)
            write_threads(process);

        if (do_write_trace)
            write_trace(thread);

        if (do_write_frame)
            write_frame(thread.GetSelectedFrame());

    } else if (SBProcess::EventIsProcessEvent(event)) {
        StateType state = SBProcess::GetStateFromEvent(event);

        uint32_t mask = event.GetType();

        if (mask & SBProcess::eBroadcastBitStateChanged) {
            StateType state = SBProcess::GetStateFromEvent(event);
            SBProcess process = SBProcess::GetProcessFromEvent(event);

            switch (state) {
            case eStateStopped:
            {
                SBThread thread = process.GetSelectedThread();
                SBFrame frame = process.GetSelectedThread().GetSelectedFrame();
                SBLineEntry line_entry = frame.GetLineEntry();

                if (trace_all_threads) {
                    for (int i = 0; i < process.GetNumThreads(); i++) {
                        write_trace(process.GetThreadAtIndex(i));
                    }
                } else {
                    write_trace(thread);
                }
                write_frame(frame);
                // fprintf(stderr, "%s:%d:%s: kak_jumping\n", __FILE__, __LINE__, __func__);
                kak_jump(line_entry);
                break;
            }
            case eStateRunning:
            case eStateStepping:
            case eStateCrashed:
            case eStateDetached:
            case eStateExited:
            case eStateSuspended:
            {
                FILE* cmd = popen(kak_cmd_string, "w");
                if (!cmd || !jump_line)
                    break;

                // const char *filename = .GetFilename();
                char path[255];
                process.GetSelectedThread()
                    .GetSelectedFrame().GetLineEntry().GetFileSpec().GetPath(path, 255);
                // const char *rp = relpath(path);

                // fprintf(cmd, "try rmhl buffer=%s/lldb-line\n", rp);
                pclose(cmd);

                jump_line = 0;
                break;
            }
            case eStateInvalid: // if not StateChanged event (in other branches)
            case eStateUnloaded:
            case eStateConnected:
            case eStateAttaching:
            case eStateLaunching:
                break;
            }
            write_threads(process);
        }

        if (mask & SBProcess::eBroadcastBitInterrupt) {}
        if (mask & SBProcess::eBroadcastBitSTDOUT) {}
        if (mask & SBProcess::eBroadcastBitSTDERR) {}
        if (mask & SBProcess::eBroadcastBitProfileData) {}
        if (mask & SBProcess::eBroadcastBitStructuredData) {} 
    } else if (SBTarget::EventIsTargetEvent(event)) {
        uint32_t mask = event.GetType();
        SBTarget target = SBTarget::GetTargetFromEvent(event);

        if (mask & SBTarget::eBroadcastBitBreakpointChanged) {}
        if (mask & SBTarget::eBroadcastBitWatchpointChanged) {}
        if (mask & SBTarget::eBroadcastBitSymbolsLoaded) {}
        if (mask & SBTarget::eBroadcastBitSymbolsChanged) {}
        if (mask & SBTarget::eBroadcastBitModulesLoaded) {
        }

        if (mask & SBTarget::eBroadcastBitModulesUnloaded) {} 
    }
}

void log_event(SBEvent event) {
    if (SBBreakpoint::EventIsBreakpointEvent(event)) {
        BreakpointEventType type = SBBreakpoint::GetBreakpointEventTypeFromEvent(event); 
        SBBreakpoint bp = SBBreakpoint::GetBreakpointFromEvent(event); 
        SBBreakpoint found = ::debugger->GetSelectedTarget().FindBreakpointByID(bp.GetID()); 

        switch (type) {
            case eBreakpointEventTypeAdded:
                	fprintf(stderr, "eBreakpointEventTypeAdded\n"); break;
            case eBreakpointEventTypeRemoved:
                	fprintf(stderr, "eBreakpointEventTypeRemoved\n"); break;
            case eBreakpointEventTypeEnabled:
                	fprintf(stderr, "eBreakpointEventTypeEnabled\n"); break;
            case eBreakpointEventTypeDisabled:
                	fprintf(stderr, "eBreakpointEventTypeDisabled\n"); break;
            case eBreakpointEventTypeLocationsAdded:
                	fprintf(stderr, "eBreakpointEventTypeLocationsAdded\n"); break;
            case eBreakpointEventTypeLocationsRemoved:
                	fprintf(stderr, "eBreakpointEventTypeLocationsRemoved\n"); break;
            case eBreakpointEventTypeIgnoreChanged:
                	fprintf(stderr, "eBreakpointEventTypeIgnoreChanged\n"); break;
            case eBreakpointEventTypeAutoContinueChanged:
                	fprintf(stderr, "eBreakpointEventTypeAutoContinueChanged\n"); break;
            case eBreakpointEventTypeLocationsResolved:
                	fprintf(stderr, "eBreakpointEventTypeLocationsResolved\n"); break;
            case eBreakpointEventTypeCommandChanged:
                	fprintf(stderr, "eBreakpointEventTypeCommandChanged\n"); break;
            case eBreakpointEventTypeConditionChanged:
                	fprintf(stderr, "eBreakpointEventTypeConditionChanged\n"); break;
            case eBreakpointEventTypeThreadChanged:
                	fprintf(stderr, "eBreakpointEventTypeThreadChanged\n"); break;
            case eBreakpointEventTypeInvalidType:
                	fprintf(stderr, "eBreakpointEventTypeInvalidType\n"); break;
        }
    } 
    if (SBWatchpoint::EventIsWatchpointEvent(event)) {
        	fprintf(stderr, "EventIsWatchpointEvent\n");
        WatchpointEventType type = SBWatchpoint::GetWatchpointEventTypeFromEvent(event); 
        switch (type) {
            case eWatchpointEventTypeAdded:
                	fprintf(stderr, "eWatchpointEventTypeAdded\n"); break;
            case eWatchpointEventTypeRemoved:
                	fprintf(stderr, "eWatchpointEventTypeRemoved\n"); break;
            case eWatchpointEventTypeEnabled:
                	fprintf(stderr, "eWatchpointEventTypeEnabled\n"); break;
            case eWatchpointEventTypeDisabled:
                	fprintf(stderr, "eWatchpointEventTypeDisabled\n"); break;
            case eWatchpointEventTypeCommandChanged:
                	fprintf(stderr, "eWatchpointEventTypeCommandChanged\n"); break;
            case eWatchpointEventTypeConditionChanged:
                	fprintf(stderr, "eWatchpointEventTypeConditionChanged\n"); break;
            case eWatchpointEventTypeIgnoreChanged:
                	fprintf(stderr, "eWatchpointEventTypeIgnoreChanged\n"); break;
            case eWatchpointEventTypeTypeChanged:
                	fprintf(stderr, "eWatchpointEventTypeTypeChanged\n"); break;
            case eWatchpointEventTypeThreadChanged:
                	fprintf(stderr, "eWatchpointEventTypeThreadChanged\n"); break;
            case eWatchpointEventTypeInvalidType:
                	fprintf(stderr, "eWatchpointEventTypeInvalidType\n"); break;
        }
    } 
    if (SBThread::EventIsThreadEvent(event)) {
        	fprintf(stderr, "EventIsThreadEvent\n");

        uint32_t mask = event.GetType();
        if (mask & SBThread::eBroadcastBitStackChanged) 
        	fprintf(stderr, "eBroadcastBitStackChanged\n");
        if (mask & SBThread::eBroadcastBitThreadSelected) 
        	fprintf(stderr, "eBroadcastBitThreadSelected\n");
        if (mask & SBThread::eBroadcastBitSelectedFrameChanged) 
        	fprintf(stderr, "eBroadcastBitSelectedFrameChanged\n");
        if (mask & SBThread::eBroadcastBitThreadSuspended) 
        	fprintf(stderr, "eBroadcastBitThreadSuspended\n");
        if (mask & SBThread::eBroadcastBitThreadResumed) 
        	fprintf(stderr, "eBroadcastBitThreadResumed\n");
        
    } 
    if (SBProcess::EventIsProcessEvent(event)) {
        	fprintf(stderr, "EventIsProcessEvent\n");
        StateType state = SBProcess::GetStateFromEvent(event);

        uint32_t mask = event.GetType();

        if (mask & SBProcess::eBroadcastBitStateChanged) {
            StateType state = SBProcess::GetStateFromEvent(event);
            switch (state) {
            case eStateStopped:
                	fprintf(stderr, "eStateStopped\n"); break;
            case eStateInvalid:
                	fprintf(stderr, "eStateInvalid\n"); break;
            case eStateUnloaded:
                	fprintf(stderr, "eStateUnloaded\n"); break;
            case eStateConnected:
                	fprintf(stderr, "eStateConnected\n"); break;
            case eStateAttaching:
                	fprintf(stderr, "eStateAttaching\n"); break;
            case eStateLaunching:
                	fprintf(stderr, "eStateLaunching\n"); break;
            case eStateRunning:
                	fprintf(stderr, "eStateRunning\n"); break;
            case eStateStepping:
                	fprintf(stderr, "eStateStepping\n"); break;
            case eStateCrashed:
                	fprintf(stderr, "eStateCrashed\n"); break;
            case eStateDetached:
                	fprintf(stderr, "eStateDetached\n"); break;
            case eStateExited:
                	fprintf(stderr, "eStateExited\n"); break;
            case eStateSuspended:
                	fprintf(stderr, "eStateSuspended\n"); break;
            }
        }

        if (mask & SBProcess::eBroadcastBitInterrupt) 
        	fprintf(stderr, "eBroadcastBitInterrupt\n");
        if (mask & SBProcess::eBroadcastBitSTDOUT) 
        	fprintf(stderr, "eBroadcastBitSTDOUT\n");
        if (mask & SBProcess::eBroadcastBitSTDERR) 
        	fprintf(stderr, "eBroadcastBitSTDERR\n");
        if (mask & SBProcess::eBroadcastBitProfileData) 
        	fprintf(stderr, "eBroadcastBitProfileData\n");
        if (mask & SBProcess::eBroadcastBitStructuredData) 
        	fprintf(stderr, "eBroadcastBitStructuredData\n");
    } 
    if (SBTarget::EventIsTargetEvent(event)) {
        	fprintf(stderr, "EventIsTargetEvent\n");
        uint32_t mask = event.GetType();
        if (mask & SBTarget::eBroadcastBitBreakpointChanged) 
        	fprintf(stderr, "eBroadcastBitBreakpointChanged\n");
        if (mask & SBTarget::eBroadcastBitWatchpointChanged) 
        	fprintf(stderr, "eBroadcastBitWatchpointChanged\n");
        if (mask & SBTarget::eBroadcastBitSymbolsLoaded) 
        	fprintf(stderr, "eBroadcastBitSymbolsLoaded\n");
        if (mask & SBTarget::eBroadcastBitSymbolsChanged) 
        	fprintf(stderr, "eBroadcastBitSymbolsChanged\n");
        if (mask & SBTarget::eBroadcastBitModulesLoaded) 
        	fprintf(stderr, "eBroadcastBitModulesLoaded\n");
        if (mask & SBTarget::eBroadcastBitModulesUnloaded) 
        	fprintf(stderr, "eBroadcastBitModulesUnloaded\n");
    }
}

void event_loop(SBDebugger debugger) {
    SBListener listener("kak_listener");
    ::debugger = &debugger;

    uint32_t target_mask = SBTarget::eBroadcastBitBreakpointChanged
            // | SBTarget::eBroadcastBitModulesLoaded
            // | SBTarget::eBroadcastBitModulesUnloaded
            | SBTarget::eBroadcastBitWatchpointChanged
            // | SBTarget::eBroadcastBitSymbolsLoaded
            | SBTarget::eBroadcastBitSymbolsChanged;

    uint32_t thread_mask = SBThread::eBroadcastBitSelectedFrameChanged
            | SBThread::eBroadcastBitStackChanged
            | SBThread::eBroadcastBitThreadResumed
            | SBThread::eBroadcastBitThreadSelected
            | SBThread::eBroadcastBitThreadSuspended;

    uint32_t process_mask = SBProcess::eBroadcastBitStateChanged
            | SBProcess::eBroadcastBitInterrupt
            | SBProcess::eBroadcastBitSTDOUT
            | SBProcess::eBroadcastBitSTDERR
            | SBProcess::eBroadcastBitProfileData
            | SBProcess::eBroadcastBitStructuredData;


    listener.StartListeningForEventClass(debugger,
            SBTarget::GetBroadcasterClassName(), target_mask); 
    listener.StartListeningForEventClass(debugger,
            SBThread::GetBroadcasterClassName(), thread_mask); 
    listener.StartListeningForEventClass(debugger,
            SBProcess::GetBroadcasterClassName(), process_mask); 

    SBEvent event;
    while (running) {
        if (listener.WaitForEvent(UINT32_MAX, event)) {
            log_event(event); 
            handle_event(event);
        }
    }
}

enum Command {
    invalid,
    array_to_pointer,
    toggle_trace_all,
    var_depth,
    jump,
    show_value,
    frame_select,
    breakpoint,
};

Command command_from_string(const char *str) {
    if (strcmp(str, "array_to_pointer") == 0) {
        return array_to_pointer;
    }
    if (strcmp(str, "toggle_trace_all") == 0) {
        return toggle_trace_all;
    }
    if (strcmp(str, "var_depth") == 0) {
        return var_depth;
    }
    if (strcmp(str, "jump") == 0) {
        return jump;
    }
    if (strcmp(str, "show_value") == 0) {
        return show_value;
    }
    if (strcmp(str, "frame_select") == 0) {
        return frame_select;
    }
    if (strcmp(str, "breakpoint") == 0) {
        return breakpoint;
    }

    return invalid;
}

class KakCmd : public SBCommandPluginInterface {
public:
    virtual bool DoExecute(SBDebugger debugger, char ** command,
                           SBCommandReturnObject & result) override {
        uint32_t depth;
        FILE *file = fopen(path_frame, "a");
        SBValue val;
        SBThread thread;

        switch (command_from_string(command[0])) {
        case invalid:
            break;
        case toggle_trace_all:
            trace_all_threads = !trace_all_threads;
            break;
        case var_depth:
            depth = atoi(command[1]);
            frame_max_depth = depth;
            write_frame(::debugger->GetSelectedTarget()
                        .GetProcess().GetSelectedThread()
                        .GetSelectedFrame());
            break;
        case show_value:
            val = ::debugger->GetSelectedTarget()
                    .GetProcess().GetSelectedThread()
                    .GetSelectedFrame().GetValueForVariablePath(command[1]);

            depth = atoi(command[2]);
            write_value(file, val, depth + 1);
           break;
        case array_to_pointer:
            val = ::debugger->GetSelectedTarget()
                    .GetProcess().GetSelectedThread()
                    .GetSelectedFrame().GetValueForVariablePath(command[1]);

            depth = atoi(command[2]);
            write_value(file, val, depth + 1);
            break;
        case jump:
            kak_jump(::debugger->GetSelectedTarget()
                        .GetProcess().GetSelectedThread()
                        .GetSelectedFrame().GetLineEntry());
            break;
        // case frame_select:
        //     thread = ::debugger->GetSelectedTarget()
        //                 .GetProcess().GetSelectedThread();
        //     thread.SetSelectedFrame(atoi(command[1] - 1)); 
        //     write_frame(thread.GetSelectedFrame());
        //     break;
        case breakpoint:
            write_breakpoints();
            break;
        }
        fclose(file);
        return true;
    }
};

namespace lldb {
#define API __attribute__((used))

API bool PluginInitialize(SBDebugger debugger)
{
    const char *sh_fifo = getenv("sh_fifo");
    const char *kak_session = getenv("kak_session");
    pwd = getenv("PWD");

    if (kak_session == NULL || sh_fifo == NULL)
        return false;

    snprintf(kak_cmd_string, 64, "kak -p %s", kak_session);
    snprintf(path_breakpoints, 64, "%s/breakpoints", sh_fifo);
    snprintf(path_watchpoints, 64, "%s/watchpoints", sh_fifo);
    snprintf(path_threads, 64, "%s/threads", sh_fifo);
    snprintf(path_frames, 64, "%s/trace", sh_fifo);
    snprintf(path_frame, 64, "%s/frame", sh_fifo);

    debugger.GetCommandInterpreter().AddCommand("kak", new KakCmd(), "kakoune commands");

    std::thread(event_loop, debugger).detach();
    return true;
}

API bool PluginTerminate()
{
    running = false;
    return true;
}

}

