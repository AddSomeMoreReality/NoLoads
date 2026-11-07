// Pre-script for analyzeHeadless: analysis options for SkyrimSE.exe (tools/ghidra, docs/GHIDRA.md).
// - The PE loader leaves the compiler "unknown" for SkyrimSE.exe 1.6.1170, and then the MSVC-only analyzers
//   (Windows x86 PE RTTI Analyzer and others) refuse to run. The game is built with MSVC.
//   Analyzers are chosen when the program is opened, so run this with -import ... -noanalysis
//   and analyze in a second analyzeHeadless call (-process), see docs/GHIDRA.md.
// - "Create Address Tables" ran out of an 8 GB heap in the GUI (05.11.2026); switch tables are still
//   recovered by the decompiler switch analysis.
//@category CellLoadResearch

import ghidra.app.script.GhidraScript;

public class ClrAnalysisOptions extends GhidraScript {
	@Override
	public void run() throws Exception {
		String before = currentProgram.getCompiler();
		// PEUtil.isVisualStudioOrClangPe compares with CompilerEnum.VisualStudio.toString()
		currentProgram.setCompiler("visualstudio:unknown");
		setAnalysisOption(currentProgram, "Create Address Tables", "false");
		println("ClrAnalysisOptions: compiler " + before + " -> " + currentProgram.getCompiler() +
			", Create Address Tables disabled");
	}
}
