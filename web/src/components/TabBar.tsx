import { Icon } from "./Icon";
import { NAV } from "../nav";

/**
 * Thumb-reach section switcher. Desktop keeps the top tabs; phones get
 * this fixed bar so Live / Network / Save stay reachable one-handed
 * next to a rack (DESIGN_SYSTEM.md §5).
 */
export function TabBar({ route }: { route: string }) {
  return (
    <nav class="tabbar" aria-label="Sections">
      {NAV.map((r) => (
        <a
          key={r.id}
          class="tabbar__item"
          href={`#/${r.id}`}
          aria-current={route === r.id ? "page" : undefined}
        >
          <Icon name={r.icon} size={16} />
          <span>{r.short}</span>
        </a>
      ))}
    </nav>
  );
}
