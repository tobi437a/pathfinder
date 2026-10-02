// Name functions from Geode's GeometryDash.bro (its `win 0x...` addresses). Args: <logFile> <broFile>
// Headless replacement for the bindings repo's interactive SyncBromaScript: names + class
// namespaces only, no signatures or struct types. Never renames a function that already has a
// non-default name, so it is safe to re-run and safe after SyncBromaScript.
// @category Pathfinder

import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;

public class ImportBroma extends GhidraScript {
	private static final Pattern CLASS = Pattern.compile("^(?:\\[\\[.*?\\]\\]\\s*)?class\\s+([\\w:]+)");
	private static final Pattern WIN = Pattern.compile("\\bwin\\s+(0x[0-9a-fA-F]+)");
	private static final Pattern NAME = Pattern.compile("(~?\\w+)\\s*$");

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		int named = 0, created = 0, kept = 0, failed = 0;
		String cls = null;
		try (PrintWriter log = new PrintWriter(args[0], "UTF-8")) {
			for (String line : Files.readAllLines(Path.of(args[1]))) {
				Matcher c = CLASS.matcher(line);
				if (c.find()) { cls = c.group(1); continue; }
				if (line.startsWith("}")) { cls = null; continue; }

				Matcher w = WIN.matcher(line);
				int paren = line.indexOf('(');
				int eq = line.lastIndexOf(" = ");
				if (!w.find() || paren < 0 || eq < paren) continue;
				Matcher n = NAME.matcher(line.substring(0, paren));
				if (!n.find() || n.group(1).equals("operator")) continue;
				String name = n.group(1);
				String full = (cls == null ? "" : cls + "::") + name;

				try {
					Address a = GdUtil.toAddress(currentProgram, w.group(1));
					Function f = getFunctionAt(a);
					if (f == null) {
						new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
						f = createFunction(a, null);
						if (f == null) { log.println("FAIL create " + full + " @ " + w.group(1)); failed++; continue; }
						created++;
					}
					SourceType src = f.getSymbol().getSource();
					if (src != SourceType.DEFAULT && src != SourceType.ANALYSIS) { kept++; continue; }

					Namespace ns = cls == null ? currentProgram.getGlobalNamespace()
						: NamespaceUtils.createNamespaceHierarchy(cls, null, currentProgram, SourceType.IMPORTED);
					f.setParentNamespace(ns);
					f.setName(name, SourceType.IMPORTED);
					named++;
				} catch (Exception e) {
					log.println("FAIL " + full + " @ " + w.group(1) + ": " + e);
					failed++;
				}
			}
			log.printf("named=%d (of which newly created=%d) kept=%d failed=%d%n", named, created, kept, failed);
		}
		println(String.format("ImportBroma: named=%d created=%d kept=%d failed=%d", named, created, kept, failed));
	}
}
