# Re: [PATCH v2] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove

| Field | Value |
| :--- | :--- |
| From | patchwork-bot+netdevbpf@kernel.org |
| To | author-a-alt <author-a-alt@example.com> |
| Cc | mani@kernel.org, davem@davemloft.net, edumazet@google.com, kuba@kernel.org, pabeni@redhat.com, horms@kernel.org, luca@lucaweiss.eu, andersson@kernel.org, linux-arm-msm@vger.kernel.org, netdev@vger.kernel.org, linux-kernel@vger.kernel.org, author-a@example.com |
| Date | Tue, 09 Jun 2026 02:00:09 +0000 |
| Message-ID | `178097040939.1753037.3386694182969051861.git-patchwork-notify@kernel.org` |
| In-Reply-To | `20260604064801.1180388-1-author-a-alt@example.com` |
| List-ID | <netdev.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/178097040939.1753037.3386694182969051861.git-patchwork-notify@kernel.org/` |

---

Hello:

This patch was applied to netdev/net.git (main)
by Jakub Kicinski <kuba@kernel.org>:

On Thu,  4 Jun 2026 14:48:01 +0800 you wrote:
> From: Author A <author-a@example.com>
> 
> In qrtr_port_remove(), the socket reference count is decremented via
> __sock_put() before the port is removed from the qrtr_ports XArray and
> before the RCU grace period elapses.
> 
> This breaks the fundamental RCU update paradigm. It exposes a race
> window where a concurrent RCU reader (such as qrtr_reset_ports() or
> qrtr_port_lookup()) can obtain a pointer to the socket from the XArray,
> and attempt to call sock_hold() on a socket whose reference count has
> already dropped to zero.
> 
> [...]

Here is the summary with links:
  - [v2] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove
    https://git.kernel.org/netdev/net/c/a2171131ecda

You are awesome, thank you!
-- 
Deet-doot-dot, I am a bot.
https://korg.docs.kernel.org/patchwork/pwbot.html
