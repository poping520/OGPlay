package probe;
public final class Main {
    private static final CaseGroup[] GROUPS = {
        new BoundaryStaticCases(), new BoundaryInstanceCases(), new CallCases(),
        new VersionCases(),        new LibraryCases(),          new ClassObjectCases(),
        new FieldsCases(),         new StringsCases(),          new ArraysCases(),
        new ReferencesCases(),     new ExceptionsCases(),       new ReflectionCases(),
        new MonitorNioCases(),     new RegistrationCases(),     new JavaVmCases()};
    public static void main(String[] args) {
        boolean list = args.length == 1 && args[0].equals("--list");
        String group = null, selected = null;
        java.util.Set<String> batch = null;
        if (args.length == 2 && args[0].equals("--group"))
            group = args[1];
        else if (args.length == 2 && args[0].equals("--case"))
            selected = args[1];
        else if (args.length == 2 && args[0].equals("--cases"))
            batch = new java.util.HashSet<String>(java.util.Arrays.asList(args[1].split(",")));
        else if (args.length != 0 && !list)
            throw new IllegalArgumentException("--list | --group g | --case id");
        int passed = 0, total = 0;
        for (CaseGroup cases : GROUPS)
            for (String id : cases.ids()) {
                if (list) {
                    System.out.println(id);
                    continue;
                }
                if (group != null && !id.startsWith(group + "."))
                    continue;
                if (selected != null && !id.equals(selected))
                    continue;
                if (batch != null && !batch.contains(id))
                    continue;
                total++;
                try {
                    System.loadLibrary("jniprobe");
                    cases.run(id);
                    System.out.println("PASS " + id);
                    passed++;
                } catch (Throwable e) {
                    System.out.println("FAIL " + id + ": " + e);
                }
            }
        if (list)
            return;
        if (total == 0)
            throw new IllegalArgumentException("no matching case");
        System.out.println("DONE " + passed + "/" + total);
    }
}
