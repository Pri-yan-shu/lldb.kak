#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include <lldb/API/LLDB.h>


using namespace lldb;

bool running = true;
const char *kak_session = nullptr;
char kak_cmd_string[32] = "";

char path_breakpoints[64];
char path_watchpoints[64];
char path_threads[64];
char path_frames[64];
char path_frame[64];

bool trace_all_threads = false;
uint8_t trace_max_depth = 0;
uint8_t frame_max_depth = 1;

SBDebugger *debugger = nullptr;
FILE* kak_send = nullptr;

void kak_jump(SBLineEntry line_entry) {
    FILE* cmd = popen(kak_cmd_string, "w");
    SBFileSpec spec = line_entry.GetFileSpec();

    if (spec.Exists()) {
        fprintf(cmd, "eval -try-client client0 e %s %u %u\n",
                spec.GetFilename(), line_entry.GetLine(),
                line_entry.GetColumn());
    } else {
        fputs("fail no file exists\n", cmd);
    }
    pclose(cmd);
}

void update_line_flags() {
    SBTarget target = ::debugger->GetSelectedTarget();
    for (int i = 0; i < target.GetNumBreakpoints(); i++) {
        SBBreakpoint bp = target.GetBreakpointAtIndex(i); 
        for (int j = 0; j < bp.GetNumLocations(); j++) {
            SBBreakpointLocation loc = bp.GetLocationAtIndex(j); 
            SBLineEntry line = loc.GetAddress().GetLineEntry();
            SBFileSpec file = line.GetFileSpec();

            const char *filename = file.GetFilename();
            const char *dir = file.GetDirectory();
            const int line_no = line.GetLine();
            const char *color = bp.IsEnabled() && loc.IsEnabled() ? "bright-red" : "blue"; 
        }
    }
}

void bp_location_line_flags(SBEvent event) {
    BreakpointEventType type = SBBreakpoint::GetBreakpointEventTypeFromEvent(event);
    char flag[256];
    const char *add = "-add";
    const char *color = "bright-red";

    switch (type) {
    case eBreakpointEventTypeDisabled:
        add = "-remove";
    case eBreakpointEventTypeEnabled:
        {
            SBBreakpoint bp = SBBreakpoint::GetBreakpointFromEvent(event); 
            for (int i = 0; i < bp.GetNumLocations(); i++) {
                SBBreakpointLocation loc = bp.GetLocationAtIndex(i); 
                color = loc.IsEnabled() ? "bright-red" : "bright-black";
                const char *rev_color = loc.IsEnabled() ? "bright-black" : "bright-red";

                SBLineEntry line = loc.GetAddress().GetLineEntry();
                SBFileSpec file = line.GetFileSpec();

                const char *filename = file.GetFilename();
                const char *dir = file.GetDirectory();
                const int line_no = line.GetLine();

                snprintf(flag, 256, "set-option %s buffer=%s lldb_flags %d|{%s}●\n", add, filename, line_no, color);
            }
        }
        break;
    case eBreakpointEventTypeLocationsRemoved:
        add = "-remove";
    case eBreakpointEventTypeLocationsAdded:
        {
            int num_loc = SBBreakpoint::GetNumBreakpointLocationsFromEvent(event); 
            for (int i = 0; i < num_loc; i++) {
                SBBreakpointLocation loc = SBBreakpoint::GetBreakpointLocationAtIndexFromEvent(event, num_loc) ;
                SBLineEntry line = loc.GetAddress().GetLineEntry();
                SBFileSpec file = line.GetFileSpec();

                const char *filename = file.GetFilename();
                const char *dir = file.GetDirectory();
                const int line_no = line.GetLine();
                if (!loc.IsEnabled()) {
                    add = "bright-black";
                }
                snprintf(flag, 256, "set-option %s buffer=%s lldb_flags %d|{%s}●\n", add, filename, line_no, color);
            }
        }
        break;
    case eBreakpointEventTypeAdded:
    case eBreakpointEventTypeRemoved:
    case eBreakpointEventTypeIgnoreChanged:
    case eBreakpointEventTypeInvalidType:
    case eBreakpointEventTypeThreadChanged:
    case eBreakpointEventTypeAutoContinueChanged:
    case eBreakpointEventTypeCommandChanged:
    case eBreakpointEventTypeConditionChanged:
    case eBreakpointEventTypeLocationsResolved:
        break;
    }
}

void breakpoints_line_flags(SBEvent event) {
    BreakpointEventType type = SBBreakpoint::GetBreakpointEventTypeFromEvent(event); 
    SBBreakpoint bp = SBBreakpoint::GetBreakpointFromEvent(event);

    std::cerr << "breakpoints_line_flags num_locations:" << bp.GetNumLocations() << "\n";
    const char *add = "-add";
    const char *color = "bright-red";
    const char *prev_color = NULL;

    switch (type) {
    case eBreakpointEventTypeRemoved:
        add = "-remove";
        if (!bp.IsEnabled())
            color = "blue";
    case eBreakpointEventTypeDisabled:
        color = "blue";
        prev_color = "red";
    case eBreakpointEventTypeIgnoreChanged:
        break;
    case eBreakpointEventTypeInvalidType:
    case eBreakpointEventTypeAdded:
    case eBreakpointEventTypeLocationsAdded:
    case eBreakpointEventTypeLocationsRemoved:
    case eBreakpointEventTypeLocationsResolved:
    case eBreakpointEventTypeEnabled:
    case eBreakpointEventTypeCommandChanged:
    case eBreakpointEventTypeConditionChanged:
    case eBreakpointEventTypeThreadChanged:
    case eBreakpointEventTypeAutoContinueChanged:
        break;
    default:
        break;
    }

    FILE* cmd = popen(kak_cmd_string, "w");
    // for (int i = 0; i < SBBreakpoint::GetNumBreakpointLocationsFromEvent(event); i++) {
        // SBBreakpointLocation loc = SBBreakpoint::GetBreakpointLocationAtIndexFromEvent(event, i); 
    for (int i = 0; i < bp.GetNumLocations(); i++) {
        SBBreakpointLocation loc = bp.GetLocationAtIndex(i); 
        SBLineEntry line = loc.GetAddress().GetLineEntry();
        SBFileSpec file = line.GetFileSpec();

        const char *filename = file.GetFilename();
        const char *dir = file.GetDirectory();
        const int line_no = line.GetLine();

        char flag[256];

        if (prev_color) {
            snprintf(flag, 256, "set-option -remove buffer=%s lldb_flags %d|{%s}●\n", filename, line_no, prev_color);
            std::cerr << flag;
            FILE* cmd = popen(kak_cmd_string, "w");
            if (cmd) {
                fputs(flag, cmd);
                pclose(cmd);
            }
        }
        snprintf(flag, 256, "set-option %s buffer=%s lldb_flags %d|{%s}●\n", add, filename, line_no, color);
        std::cerr << flag;
        if (cmd) {
            fputs(flag, cmd);
        }
    }

    pclose(cmd);
}

void watchpoints_line_flags(SBEvent event) {
    std::cerr << "watchpoints_line_flags\n";
}

void write_breakpoints() {
    std::ofstream file(path_breakpoints, std::ios::trunc | std::ios::out);

    SBTarget target = ::debugger->GetSelectedTarget();

    for (int i = 0; i < target.GetNumBreakpoints(); i++) {
        SBBreakpoint bp = target.GetBreakpointAtIndex(i);
        for (int j = 0; j < bp.GetNumLocations(); j++) {
            SBBreakpointLocation location = bp.GetLocationAtIndex(j); 
            SBAddress address = location.GetAddress();
            SBLineEntry line = address.GetLineEntry();

            file << bp.GetID() << '.' << j + 1 << ' '
            << address.GetFunction().GetDisplayName() << ' '
            << bp.GetHitCount() << ' '
            << bp.GetIgnoreCount() << ' '
            << bp.IsOneShot() << ' '
            // << address.GetFileAddress()
            << line.GetFileSpec().GetFilename()
            << ":"
            << line.GetLine()
            << ":"
            << line.GetColumn()
            << std::endl; 
        }
    }
}

void write_watchpoints() {
    std::ofstream file(path_watchpoints, std::ios::trunc | std::ios::out);

    SBTarget target = ::debugger->GetSelectedTarget();

    for (int i = 0; i < target.GetNumWatchpoints(); i++) {
        SBWatchpoint wp = target.GetWatchpointAtIndex(i); 
        const char *rw = wp.IsWatchingReads() ?
            (wp.IsWatchingWrites() ? "rw" : "r")
            : (wp.IsWatchingWrites() ? "w" : "");

        file
        << wp.GetID() << ' '
        << wp.GetWatchSpec() << ' '
        << wp.GetWatchAddress() << ' '
        << wp.GetWatchSize() << ' '
        << wp.GetHitCount() << ' '
        << wp.GetIgnoreCount() << ' '
        << wp.GetCondition() << ' '
        << wp.IsEnabled() << ' '
        << rw;
    }
}

void write_threads(SBProcess process) {
    std::ofstream file(path_threads, std::ios::trunc | std::ios::out);

    // SBTarget target = ::debugger->GetSelectedTarget();
    for (int i = 0; i < process.GetNumThreads(); i++) {
        SBThread thread = process.GetThreadAtIndex(i);

        std::string flags;
        SBValue retval = thread.GetStopReturnValue();
        if (retval.IsValid()) {
            flags += retval.GetValue();
        }

        if (thread.IsStopped()) 
            flags += " s";
        if (thread.IsSuspended()) 
            flags += " S";

        file
        << thread.GetThreadID() << ' '
        << thread.GetName() << ' '
        << thread.GetNumFrames() << ' '
        << thread.GetSelectedFrame().GetDisplayFunctionName() << ' '
        << flags
        << std::endl;
    }
}

void write_trace(SBThread thread) {
    std::ofstream file(path_frames, std::ios::trunc | std::ios::out);

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

        file
        << frame.GetFunctionName() << ' '
        << frame.GetModule().GetFileSpec().GetFilename() << ' '
        << line_entry.GetFileSpec().GetFilename() << ':'
        << line_entry.GetLine() << ':' << line_entry.GetColumn() << ' '
        << (frame.IsInlined() ? 'i' : ' ')
        << (frame == thread.GetSelectedFrame() ? '<' : ' ')
        << '\n';
    }
}

void write_value(std::ostream& os, SBValue val, uint32_t max_depth, uint32_t depth = 0) {
    SBStream path;
    bool has_path = val.GetExpressionPath(path);
    if (has_path) {
        os << path.GetData();
    } else {
        os << val.GetName();
    }
    os << "\t: " << val.GetTypeName();

    const char *summary = val.GetSummary();

    if (summary == nullptr)
        summary = val.GetValue();

    if (summary != nullptr)
        os << "\t= " << summary;

    const uint32_t num_children = val.GetNumChildren();

    os << '\n';
    if (depth >= max_depth || num_children == 0 || num_children > 16) {
        return;
    }

    for (int i = 0; i < num_children; i++) {
        SBValue child = val.GetChildAtIndex(i);
        for (int j = 0; j <= depth; j++) 
            os << '\t';
        write_value(os, child, max_depth, depth + 1);
    }
}

void write_frame(SBFrame frame) {
    std::ofstream file(path_frame, std::ios::trunc | std::ios::out);

    frame.GetSymbolContext(3).GetFunction().GetInstructions(frame.GetThread().GetProcess().GetTarget()); 

    // file << frame.GetDisplayFunctionName() << '\n';
    SBValueList vals = frame.GetVariables(true, true, false, false);
    for (int i = 0; i < vals.GetSize(); i++) {
        SBValue val = vals.GetValueAtIndex(i); 
        write_value(file, val, frame_max_depth);
    }
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
                watchpoints_line_flags(event);
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
                    fprintf(stderr, "%s:%d:%s: kak_jumping\n", __FILE__, __LINE__, __func__);
                    kak_jump(line_entry);
                }
                break;
            case eStateInvalid: // if not StateChanged event (in other branches)
            case eStateUnloaded:
            case eStateConnected:
            case eStateAttaching:
            case eStateLaunching:
            case eStateRunning:
            case eStateStepping:
            case eStateCrashed:
            case eStateDetached:
            case eStateExited:
            case eStateSuspended:
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
            int num_modules = SBTarget::GetNumModulesFromEvent(event); 
            std::cerr << "Num modules: " << num_modules << '\n';
            for (int i = 0; i < num_modules; i++) {
                SBModule mod = SBTarget::GetModuleAtIndexFromEvent(i, event);
                std::cerr << mod.GetFileSpec().GetFilename() << '\n';
            }
            SBProcess process = target.GetProcess();
            if (process.IsValid()) {
                SBThread thread = process.GetSelectedThread();
                write_trace(thread);
                write_frame(thread.GetSelectedFrame());
                write_threads(process);
            }
        }

        if (mask & SBTarget::eBroadcastBitModulesUnloaded) {} 
    }
}

void log_event(SBEvent event) {
    // std::cerr << "event.GetType(): " << event.GetType() << '\n';
    if (SBBreakpoint::EventIsBreakpointEvent(event)) {
        BreakpointEventType type = SBBreakpoint::GetBreakpointEventTypeFromEvent(event); 
        SBBreakpoint bp = SBBreakpoint::GetBreakpointFromEvent(event); 
        SBBreakpoint found = ::debugger->GetSelectedTarget().FindBreakpointByID(bp.GetID()); 

        std::cerr << "EventIsBreakpointEvent\n";
        std::cerr << "bp_locs: " << bp.GetNumLocations()
        << " ev_locs: " << SBBreakpoint::GetNumBreakpointLocationsFromEvent(event) << '\n'
        << " found_locs: " << found.GetNumLocations() << '\n';

        switch (type) {
            case eBreakpointEventTypeAdded:
                std::cerr << "eBreakpointEventTypeAdded\n";
                break;
            case eBreakpointEventTypeRemoved:
                std::cerr << "eBreakpointEventTypeRemoved\n";
                break;
            case eBreakpointEventTypeEnabled:
                std::cerr << "eBreakpointEventTypeEnabled\n";
                break;
            case eBreakpointEventTypeDisabled:
                std::cerr << "eBreakpointEventTypeDisabled\n";
                break;
            case eBreakpointEventTypeLocationsAdded:
                std::cerr << "eBreakpointEventTypeLocationsAdded\n";
                break;
            case eBreakpointEventTypeLocationsRemoved:
                std::cerr << "eBreakpointEventTypeLocationsRemoved\n";
                break;
            case eBreakpointEventTypeIgnoreChanged:
                std::cerr << "eBreakpointEventTypeIgnoreChanged\n";
                break;
            case eBreakpointEventTypeAutoContinueChanged:
                std::cerr << "eBreakpointEventTypeAutoContinueChanged\n";
                break;
            case eBreakpointEventTypeLocationsResolved:
                std::cerr << "eBreakpointEventTypeLocationsResolved\n";
                break;
            case eBreakpointEventTypeCommandChanged:
                std::cerr << "eBreakpointEventTypeCommandChanged\n";
                break;
            case eBreakpointEventTypeConditionChanged:
                std::cerr << "eBreakpointEventTypeConditionChanged\n";
                break;
            case eBreakpointEventTypeThreadChanged:
                std::cerr << "eBreakpointEventTypeThreadChanged\n";
                break;
            case eBreakpointEventTypeInvalidType:
                std::cerr << "eBreakpointEventTypeInvalidType\n";
                break;
        }
    } 
    if (SBWatchpoint::EventIsWatchpointEvent(event)) {
        std::cerr << "EventIsWatchpointEvent\n";
        WatchpointEventType type = SBWatchpoint::GetWatchpointEventTypeFromEvent(event); 
        switch (type) {
            case eWatchpointEventTypeAdded:
                std::cerr << "eWatchpointEventTypeAdded\n";
                break;
            case eWatchpointEventTypeRemoved:
                std::cerr << "eWatchpointEventTypeRemoved\n";
                break;
            case eWatchpointEventTypeEnabled:
                std::cerr << "eWatchpointEventTypeEnabled\n";
                break;
            case eWatchpointEventTypeDisabled:
                std::cerr << "eWatchpointEventTypeDisabled\n";
                break;
            case eWatchpointEventTypeCommandChanged:
                std::cerr << "eWatchpointEventTypeCommandChanged\n";
                break;
            case eWatchpointEventTypeConditionChanged:
                std::cerr << "eWatchpointEventTypeConditionChanged\n";
                break;
            case eWatchpointEventTypeIgnoreChanged:
                std::cerr << "eWatchpointEventTypeIgnoreChanged\n";
                break;
            case eWatchpointEventTypeTypeChanged:
                std::cerr << "eWatchpointEventTypeTypeChanged\n";
                break;
            case eWatchpointEventTypeThreadChanged:
                std::cerr << "eWatchpointEventTypeThreadChanged\n";
                break;
            case eWatchpointEventTypeInvalidType:
                std::cerr << "eWatchpointEventTypeInvalidType\n";
                break;
        }
    } 
    if (SBThread::EventIsThreadEvent(event)) {
        std::cerr << "EventIsThreadEvent\n";

        uint32_t mask = event.GetType();
        if (mask & SBThread::eBroadcastBitStackChanged) {
            std::cerr << "eBroadcastBitStackChanged\n";
        }

        if (mask & SBThread::eBroadcastBitThreadSelected) {
            std::cerr << "eBroadcastBitThreadSelected\n";
        }

        if (mask & SBThread::eBroadcastBitSelectedFrameChanged) {
            std::cerr << "eBroadcastBitSelectedFrameChanged\n";
        }

        if (mask & SBThread::eBroadcastBitThreadSuspended) {
            std::cerr << "eBroadcastBitThreadSuspended\n";
        }

        if (mask & SBThread::eBroadcastBitThreadResumed) {
            std::cerr << "eBroadcastBitThreadResumed\n";
        }
    } 
    if (SBProcess::EventIsProcessEvent(event)) {
        std::cerr << "EventIsProcessEvent\n";
        StateType state = SBProcess::GetStateFromEvent(event);

        uint32_t mask = event.GetType();

        if (mask & SBProcess::eBroadcastBitStateChanged) {
            StateType state = SBProcess::GetStateFromEvent(event);
            switch (state) {
                case eStateStopped:
                    std::cerr << "eStateStopped\n";
                    break;
                case eStateInvalid: // if not StateChanged event (in other branches)
                    std::cerr << "eStateInvalid\n";
                    break;
                case eStateUnloaded:
                    std::cerr << "eStateUnloaded\n";
                    break;
                case eStateConnected:
                    std::cerr << "eStateConnected\n";
                    break;
                case eStateAttaching:
                    std::cerr << "eStateAttaching\n";
                    break;
                case eStateLaunching:
                    std::cerr << "eStateLaunching\n";
                    break;
                case eStateRunning:
                    std::cerr << "eStateRunning\n";
                    break;
                case eStateStepping:
                    std::cerr << "eStateStepping\n";
                    break;
                case eStateCrashed:
                    std::cerr << "eStateCrashed\n";
                    break;
                case eStateDetached:
                    std::cerr << "eStateDetached\n";
                    break;
                case eStateExited:
                    std::cerr << "eStateExited\n";
                    break;
                case eStateSuspended:
                    std::cerr << "eStateSuspended\n";
                    break;
            }
        }

        if (mask & SBProcess::eBroadcastBitInterrupt) {
            std::cerr << "eBroadcastBitInterrupt\n";
        }

        if (mask & SBProcess::eBroadcastBitSTDOUT) {
            std::cerr << "eBroadcastBitSTDOUT\n";
        }

        if (mask & SBProcess::eBroadcastBitSTDERR) {
            std::cerr << "eBroadcastBitSTDERR\n";
        }

        if (mask & SBProcess::eBroadcastBitProfileData) {
            std::cerr << "eBroadcastBitProfileData\n";
        }

        if (mask & SBProcess::eBroadcastBitStructuredData) {
            std::cerr << "eBroadcastBitStructuredData\n";
        } 
    } 
    if (SBTarget::EventIsTargetEvent(event)) {
        std::cerr << "EventIsTargetEvent\n";
        uint32_t mask = event.GetType();
        if (mask & SBTarget::eBroadcastBitBreakpointChanged) {
            std::cerr << "eBroadcastBitBreakpointChanged\n";
        }

        if (mask & SBTarget::eBroadcastBitWatchpointChanged) {
            std::cerr << "eBroadcastBitWatchpointChanged\n";
        }

        if (mask & SBTarget::eBroadcastBitSymbolsLoaded) {
            std::cerr << "eBroadcastBitSymbolsLoaded\n";
        }

        if (mask & SBTarget::eBroadcastBitSymbolsChanged) {
            std::cerr << "eBroadcastBitSymbolsChanged\n";
        }

        if (mask & SBTarget::eBroadcastBitModulesLoaded) {
            std::cerr << "eBroadcastBitModulesLoaded\n";
        }

        if (mask & SBTarget::eBroadcastBitModulesUnloaded) {
            std::cerr << "eBroadcastBitModulesUnloaded\n";
        } 
    }
}

void event_loop(SBDebugger debugger) {
    SBListener listener("my_listener");
    ::debugger = &debugger;
    // ::listener = &listener;

    uint32_t target_mask = SBTarget::eBroadcastBitBreakpointChanged
            | SBTarget::eBroadcastBitModulesLoaded
            | SBTarget::eBroadcastBitModulesUnloaded
            | SBTarget::eBroadcastBitWatchpointChanged
            | SBTarget::eBroadcastBitSymbolsLoaded
            | SBTarget::eBroadcastBitSymbolsChanged;

    uint32_t thread_mask = SBThread::eBroadcastBitSelectedFrameChanged
            | SBThread::eBroadcastBitStackChanged
            | SBThread::eBroadcastBitThreadResumed
            | SBThread::eBroadcastBitThreadSelected
            | SBThread::eBroadcastBitThreadSuspended;

    uint32_t process_mask = SBProcess::eBroadcastBitStateChanged
            | SBProcess::eBroadcastBitInterrupt;

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
    toggle_trace_all,
    var_depth,
    jump,
    show_value,
    frame_select,
};

Command command_from_string(const char *str) {
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

    return invalid;
}

class KaklCmd : public SBCommandPluginInterface {
public:
    virtual bool DoExecute(SBDebugger debugger, char ** command,
                           SBCommandReturnObject & result) override {
        uint32_t depth;
        std::ofstream file(path_frame, std::ios::app);
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
            // fprintf(stderr, "%s:%d:%s: showing_value\n", __FILE__, __LINE__, __func__);
            write_value(file, val, depth + 1);
            break;
        case jump:
            kak_jump(::debugger->GetSelectedTarget()
                     .GetProcess().GetSelectedThread()
                     .GetSelectedFrame().GetLineEntry());
            break;
        case frame_select:
            thread = ::debugger->GetSelectedTarget()
                            .GetProcess().GetSelectedThread();
            thread.SetSelectedFrame(atoi(command[1] - 1)); 
            write_frame(thread.GetSelectedFrame());
            // ::listener->AddEvent(SBEvent());
            break;
        }

        return true;
    }
};

namespace lldb {
#define API __attribute__((used))

API bool PluginInitialize(SBDebugger debugger)
{
    const char *sh_fifo = std::getenv("sh_fifo");
    kak_session = std::getenv("kak_session");

    if (kak_session == NULL || sh_fifo == NULL)
        return false;

    ::snprintf(kak_cmd_string, 32, "kak -p %s", kak_session);
    ::snprintf(path_breakpoints, 64, "%s/breakpoints", sh_fifo);
    ::snprintf(path_watchpoints, 64, "%s/watchpoints", sh_fifo);
    ::snprintf(path_threads, 64, "%s/threads", sh_fifo);
    ::snprintf(path_frames, 64, "%s/trace", sh_fifo);
    ::snprintf(path_frame, 64, "%s/frame", sh_fifo);

    debugger.GetCommandInterpreter().AddCommand("kak", new KaklCmd(), "kakl command interface");

    std::cerr << "plugin loaded\n";

    std::thread(event_loop, debugger).detach();
    return true;
}

API bool PluginTerminate()
{
    running = false;
    return true;
}

}

