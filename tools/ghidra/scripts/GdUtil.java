// Shared helpers for the headless GD scripts (not a script itself).
// @category Pathfinder

import java.util.ArrayList;
import java.util.List;

import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.Symbol;

public class GdUtil {
	/**
	 * Resolve a target to functions. Accepts:
	 *   PlayerObject::updateJump   (every overload)
	 *   0x38b900                   (RVA, i.e. the `win` address in GeometryDash.bro)
	 *   0x14038b900                (absolute address)
	 * An address inside a function resolves to the containing function.
	 */
	public static List<Function> resolve(Program p, String target) {
		List<Function> out = new ArrayList<>();
		Address a = toAddress(p, target);
		if (a != null) {
			Function f = p.getFunctionManager().getFunctionContaining(a);
			if (f != null) out.add(f);
			return out;
		}
		for (Symbol s : NamespaceUtils.getSymbols(target, p)) {
			Function f = p.getFunctionManager().getFunctionAt(s.getAddress());
			if (f != null && !out.contains(f)) out.add(f);
		}
		return out;
	}

	/** Parse a hex RVA or absolute address; null if `s` is not hex. */
	public static Address toAddress(Program p, String s) {
		String h = s.toLowerCase();
		if (h.startsWith("0x")) h = h.substring(2);
		if (!h.matches("[0-9a-f]+")) return null;
		long v = Long.parseUnsignedLong(h, 16);
		long base = p.getImageBase().getOffset();
		if (v < base) v += base;
		return p.getAddressFactory().getDefaultAddressSpace().getAddress(v);
	}

	public static long rva(Program p, Address a) {
		return a.getOffset() - p.getImageBase().getOffset();
	}

	public static String describe(Program p, Function f) {
		return String.format("%s  rva=0x%x  size=%d  sig=%s",
			f.getName(true), rva(p, f.getEntryPoint()), f.getBody().getNumAddresses(),
			f.getPrototypeString(false, false));
	}
}
