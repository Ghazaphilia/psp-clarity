	.set noreorder

#include "pspimport.s"

	IMPORT_START "SystemCtrlForKernel",0x00090000
	IMPORT_FUNC  "SystemCtrlForKernel",0x159AF5CC,sctrlHENFindFunction
	IMPORT_FUNC  "SystemCtrlForKernel",0x826668E9,sctrlHENPatchSyscall
