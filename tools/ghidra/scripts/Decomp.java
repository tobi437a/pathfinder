// Decompile functions to a file. Args: <outFile> <target>...
// A target prefixed with "asm:" dumps disassembly instead of pseudo-C.
// See GdUtil.resolve for the accepted target forms.
// @category Pathfinder

import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class Decomp extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		DecompInterface ifc = new DecompInterface();
		ifc.openProgram(currentProgram);
		try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
			for (int i = 1; i < args.length; i++) {
				boolean asm = args[i].startsWith("asm:");
				String target = asm ? args[i].substring(4) : args[i];
				var funcs = GdUtil.resolve(currentProgram, target);
				if (funcs.isEmpty()) {
					w.printf("// ===== %s: NOT FOUND%n%n", target);
					continue;
				}
				for (Function f : funcs) {
					w.printf("// ===== %s%n", GdUtil.describe(currentProgram, f));
					if (asm) {
						for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true))
							w.printf("%08x  %s%n", GdUtil.rva(currentProgram, ins.getAddress()), ins);
					} else {
						DecompileResults r = ifc.decompileFunction(f, 300, monitor);
						w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
							: "// decompile failed: " + r.getErrorMessage());
					}
					w.println();
				}
			}
		}
		ifc.dispose();
		println("wrote " + args[0]);
	}
}
