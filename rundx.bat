set NOPROGRESS=1
call ice-cpp exe:debug/F.A.R.S.H-dx
if %errorlevel% neq 0 (
	pause
) else (
	debug\F.A.R.S.H-dx.exe
)