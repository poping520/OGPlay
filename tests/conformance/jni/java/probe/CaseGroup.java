package probe;
interface CaseGroup {
    String[] ids();
    void run(String id) throws Throwable;
}
