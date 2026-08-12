import type { ComponentChildren } from "preact";

/*
 * The form vocabulary. Everything here is deliberately plain: hard corners,
 * a visible border, a neon focus state, and a touch target that clears the
 * minimum. There is no soft-shadow / rounded-card layer in this system.
 */

export function Card({
  title,
  note,
  children,
  actions,
}: {
  title?: string;
  note?: ComponentChildren;
  children: ComponentChildren;
  actions?: ComponentChildren;
}) {
  return (
    <section class="card">
      {title || actions ? (
      <header class="card__head">
        {title ? <h2 class="card__title">{title}</h2> : null}
        {actions ? <div style="margin-left:auto">{actions}</div> : null}
      </header>
      ) : null}
      <div class="card__body">{children}</div>
      {note ? <p class="card__note">{note}</p> : null}
    </section>
  );
}

export function Field({
  label,
  hint,
  children,
}: {
  label: string;
  hint?: ComponentChildren;
  children: ComponentChildren;
}) {
  return (
    <label class="field">
      <span class="field__label">{label}</span>
      {children}
      {hint ? <span class="field__hint">{hint}</span> : null}
    </label>
  );
}

export function NumberField({
  label,
  hint,
  value,
  min,
  max,
  step,
  onChange,
}: {
  label: string;
  hint?: ComponentChildren;
  value: number;
  min?: number;
  max?: number;
  step?: number;
  onChange: (v: number) => void;
}) {
  return (
    <Field label={label} hint={hint}>
      <input
        type="number"
        value={value}
        min={min}
        max={max}
        step={step}
        onInput={(e) => {
          const next = Number((e.target as HTMLInputElement).value);
          if (!Number.isNaN(next)) onChange(next);
        }}
      />
    </Field>
  );
}

export function SelectField<T extends string | number>({
  label,
  hint,
  value,
  options,
  onChange,
}: {
  label: string;
  hint?: ComponentChildren;
  value: T;
  options: { value: T; label: string }[];
  onChange: (v: T) => void;
}) {
  return (
    <Field label={label} hint={hint}>
      <select
        value={String(value)}
        onChange={(e) => {
          const raw = (e.target as HTMLSelectElement).value;
          const match = options.find((o) => String(o.value) === raw);
          if (match) onChange(match.value);
        }}
      >
        {options.map((o) => (
          <option key={String(o.value)} value={String(o.value)}>
            {o.label}
          </option>
        ))}
      </select>
    </Field>
  );
}

export function TextField({
  label,
  hint,
  value,
  type = "text",
  placeholder,
  maxLength,
  onChange,
}: {
  label: string;
  hint?: ComponentChildren;
  value: string;
  type?: "text" | "password";
  placeholder?: string;
  maxLength?: number;
  onChange: (v: string) => void;
}) {
  return (
    <Field label={label} hint={hint}>
      <input
        type={type}
        value={value}
        placeholder={placeholder}
        maxLength={maxLength}
        autocomplete={type === "password" ? "current-password" : "off"}
        onInput={(e) => onChange((e.target as HTMLInputElement).value)}
      />
    </Field>
  );
}

/**
 * A two-state block rather than a soft switch. It states ON / OFF in words
 * as well as by position, so the state survives being read at a glance in a
 * dim room (DESIGN_SYSTEM.md §12).
 */
export function Toggle({
  label,
  checked,
  onChange,
}: {
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <label class="toggle">
      <input
        type="checkbox"
        checked={checked}
        onChange={(e) => onChange((e.target as HTMLInputElement).checked)}
      />
      <span class="toggle__box">
        <span class="toggle__knob" />
      </span>
      <span class="toggle__state mono">{checked ? "ON" : "OFF"}</span>
      <span>{label}</span>
    </label>
  );
}

export function Button({
  children,
  onClick,
  variant = "primary",
  type = "button",
  disabled,
}: {
  children: ComponentChildren;
  onClick?: () => void;
  variant?: "primary" | "secondary" | "danger";
  type?: "button" | "submit";
  disabled?: boolean;
}) {
  const cls = variant === "primary" ? "btn" : `btn btn--${variant}`;
  return (
    <button class={cls} type={type} onClick={onClick} disabled={disabled}>
      {children}
    </button>
  );
}

export function Readout({ label, value }: { label: string; value: ComponentChildren }) {
  return (
    <div class="readout">
      <span class="readout__label">{label}</span>
      <span class="readout__value">{value}</span>
    </div>
  );
}

export function ConfirmModal({
  title,
  body,
  confirmLabel,
  onConfirm,
  onCancel,
}: {
  title: string;
  body: ComponentChildren;
  confirmLabel: string;
  onConfirm: () => void;
  onCancel: () => void;
}) {
  return (
    <div
      class="modal__scrim"
      role="dialog"
      aria-modal="true"
      aria-label={title}
      onClick={(e) => {
        if (e.target === e.currentTarget) onCancel();
      }}
    >
      <div class="modal">
        <div class="modal__body">
          <h2 class="modal__title">{title}</h2>
          <p>{body}</p>
          <div class="btn-row">
            <Button variant="danger" onClick={onConfirm}>
              {confirmLabel}
            </Button>
            <Button variant="secondary" onClick={onCancel}>
              Cancel
            </Button>
          </div>
        </div>
      </div>
    </div>
  );
}
