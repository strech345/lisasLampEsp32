// Einstieg der Weboberfläche: App einhängen und Fokus nach Klicks abgeben.
import { mount } from 'svelte'
import App from './App.svelte'

// Nach einem Klick/Antippen geben Knöpfe, Checkboxen und Auswahllisten den Fokus wieder ab, damit kein
// Fokus-Rahmen stehen bleibt (vor allem auf dem Handy). Textfelder behalten ihn zum Tippen.
// Bei Tastaturbedienung (Tab) bleibt der Fokus erhalten, dort zeigt :focus-visible den Rahmen.
const BLUR_SELECTOR = 'button, [role="button"], input[type="checkbox"], input[type="radio"], summary'
document.addEventListener('click', (e) => {
  if (e.detail === 0) return // per Tastatur ausgelöst (Enter/Leertaste) → Fokus behalten
  const el = e.target.closest(BLUR_SELECTOR)
  if (el) el.blur()
})
document.addEventListener('change', (e) => {
  if (e.target.tagName === 'SELECT') e.target.blur()
})

mount(App, { target: document.getElementById('app') })
