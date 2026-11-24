$env:JAVA_HOME = "C:\SOURCES\ftl\tools\jdk25\jdk-25.0.5.1+2"
$env:JAVA_TOOL_OPTIONS = "-Dsettingsdir=C:\SOURCES\ftl\work\gsettings"
& "C:\SOURCES\ftl\tools\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat" C:\SOURCES\ftl\work\proj2 FTL -process FTL -noanalysis -scriptPath C:\SOURCES\ftl\work\scripts -postScript Lift.java C:\SOURCES\ftl\work\recomp\genh ALL OVERRIDE:_sys_file_ OVERRIDE:_sys_dir_ OVERRIDE:_sys_time_ OVERRIDE:_sys_thread_ OVERRIDE:_sys_mutex_ OVERRIDE:_sys_semaphore_ OVERRIDE:_sys_userdata_ OVERRIDE:_sys_input_ OVERRIDE:_sys_sound_ OVERRIDE:_sys_last_error OVERRIDE:_sys_reset_idle_timer OVERRIDE:_ios_ *> C:\SOURCES\ftl\work\lift_h.log
"LIFTFINI" | Out-File -Append C:\SOURCES\ftl\work\lift_h.log
