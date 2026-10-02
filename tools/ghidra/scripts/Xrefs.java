// List every reference to a function or address (callers, data refs). Args: <outFile> <target>...
// For a function target, references to its entry point are listed; for a bare address that is
// not a function entry (e.g. a global or a constant), references to that exact address are listed.
// @category Pathfinder

import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class Xrefs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
			for (int i = 1; i < args.length; i++) {
				Address a = GdUtil.toAddress(currentProgram, args[i]);
				if (a != null) {
					Function f = currentProgram.getFunctionManager().getFunctionAt(a);
					dump(w, f != null ? GdUtil.describe(currentProgram, f)
						: String.format("address rva=0x%x", GdUtil.rva(currentProgram, a)), a);
					continue;
				}
				var funcs = GdUtil.resolve(currentProgram, args[i]);
				if (funcs.isEmpty()) w.printf("== %s: NOT FOUND%n%n", args[i]);
				for (Function f : funcs) dump(w, GdUtil.describe(currentProgram, f), f.getEntryPoint());
			}
		}
		println("wrote " + args[0]);
	}

	private void dump(PrintWriter w, String header, Address to) {
		w.println("== " + header);
		int n = 0;
		for (Reference r : getReferencesTo(to)) {
			Address from = r.getFromAddress();
			Function caller = getFunctionContaining(from);
			w.printf("  %-10s from rva=0x%x  in %s%n", r.getReferenceType(),
				GdUtil.rva(currentProgram, from), caller == null ? "?" : caller.getName(true));
			n++;
		}
		w.printf("  (%d refs)%n%n", n);
	}
}
