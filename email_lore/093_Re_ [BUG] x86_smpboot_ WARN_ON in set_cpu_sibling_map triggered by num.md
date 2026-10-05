# Re: [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2

| Field | Value |
| :--- | :--- |
| From | Peter Zijlstra <peterz@infradead.org> |
| To | author-a-alt@example.com |
| Cc | tglx@kernel.org, mingo@redhat.com, bp@alien8.de, dave.hansen@linux.intel.com, x86@kernel.org, hpa@zytor.com, linux-kernel@vger.kernel.org |
| Date | Mon, 4 May 2026 09:48:30 +0200 |
| Message-ID | `20260504074830.GM3126523@noisy.programming.kicks-ass.net` |
| In-Reply-To | `20260429034125.37368-1-author-a-alt@example.com` |
| List-ID | <linux-kernel.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260504074830.GM3126523@noisy.programming.kicks-ass.net/` |

---


You forgot to Cc lkml.

On Wed, Apr 29, 2026 at 11:41:25AM +0800, author-a-alt@example.com wrote:
> Hi x86 maintainers,
> 
> While fuzzing the v7.0 kernel, I encountered a persistent WARNING in
> `set_cpu_sibling_map()` during boot when using the `numa=fake=2`
> command-line parameter.
> 
> The issue appears to be a logic gap in the topology consistency check.
> When `numa=fake=N` is used, it artificially divides a single physical
> package into multiple software NUMA nodes. However, the existing check
> in `set_cpu_sibling_map()` does not account for this fake NUMA state:
> 
>     if (match_pkg(c, o) && !topology_same_node(c, o))
>         WARN_ON_ONCE(topology_num_nodes_per_package() == 1);
> 
> Since `numa=fake` forces `!topology_same_node(c, o)` to be true for
> CPUs on the same package, the WARN_ON_ONCE is falsely triggered. With
> `panic_on_warn=1` enabled in many fuzzing and testing environments,
> this leads to an early boot panic.

*groan*, so ideally we'd just rip out the whole fake numa stuff
entirely, and let people use VMs to fake topology in a consistent
manner.

I'm assuming something like so helps?

diff --git a/arch/x86/kernel/smpboot.c b/arch/x86/kernel/smpboot.c
index 294a8ea60298..2447b0317f7b 100644
--- a/arch/x86/kernel/smpboot.c
+++ b/arch/x86/kernel/smpboot.c
@@ -468,6 +468,8 @@ static int x86_cluster_flags(void)
 }
 #endif
 
+static bool x86_has_fake_numa = false;
+
 static struct sched_domain_topology_level x86_topology[] = {
 	SDTL_INIT(tl_smt_mask, cpu_smt_flags, SMT),
 #ifdef CONFIG_SCHED_CLUSTER
@@ -489,7 +491,7 @@ static void __init build_sched_topology(void)
 	 * PKG domain since the NUMA domains will auto-magically create the
 	 * right spanning domains based on the SLIT.
 	 */
-	if (topology_num_nodes_per_package() > 1) {
+	if (topology_num_nodes_per_package() > 1 || x86_has_fake_numa) {
 		unsigned int pkgdom = ARRAY_SIZE(x86_topology) - 2;
 
 		memset(&x86_topology[pkgdom], 0, sizeof(x86_topology[pkgdom]));
@@ -662,6 +664,13 @@ int arch_sched_node_distance(int from, int to)
 		    topology_num_nodes_per_package() < 3)
 			return d;
 
+		/*
+		 * XXX someone needs to figure out what, if anything can be
+		 * done here.
+		 */
+		if (WARN_ON_ONCE(x86_has_fake_numa))
+			return d;
+
 		/*
 		 * Handle SNC-3 asymmetries.
 		 */
@@ -694,8 +703,10 @@ void set_cpu_sibling_map(int cpu)
 	for_each_cpu(i, cpu_sibling_setup_mask) {
 		o = &cpu_data(i);
 
-		if (match_pkg(c, o) && !topology_same_node(c, o))
-			WARN_ON_ONCE(topology_num_nodes_per_package() == 1);
+		if (match_pkg(c, o) && !topology_same_node(c, o)) {
+			if (topology_num_nodes_per_package() == 1)
+				x86_has_fake_numa = true;
+		}
 
 		if ((i == cpu) || (has_smt && match_smt(c, o)))
 			link_mask(topology_sibling_cpumask, cpu, i);
