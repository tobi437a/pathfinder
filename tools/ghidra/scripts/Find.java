// Find functions whose fully-qualified name matches a regex (case-insensitive).
// Args: <outFile> <regex>...      e.g.  "PlayerObject::.*Slope.*"
// Also reports project stats (how many functions still carry Ghidra's default FUN_ names),
// which is how to tell whether SyncBromaScript has been run.
// @category Pathfinder

import java.io.PrintWriter;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class Find extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
			int total = 0, unnamed = 0;
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				total++;
				if (f.getName().startsWith("FUN_")) unnamed++;
			}
			w.printf("# %s: %d functions, %d named, %d still FUN_*%n%n",
				currentProgram.getName(), total, total - unnamed, unnamed);

			for (int i = 1; i < args.length; i++) {
				Pattern pat = Pattern.compile(args[i], Pattern.CASE_INSENSITIVE);
				w.println("== /" + args[i] + "/");
				int n = 0;
				for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
					if (pat.matcher(f.getName(true)).find()) {
						w.println("  " + GdUtil.describe(currentProgram, f));
						if (++n >= 500) { w.println("  ... (truncated at 500)"); break; }
					}
				}
				w.println();
			}
		}
		println("wrote " + args[0]);
	}
}
