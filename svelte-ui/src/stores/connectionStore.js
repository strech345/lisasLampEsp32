// Verbindungszustand zur Lampe: true = erreichbar. Wird in App.svelte per /ping geprüft
// (beim Start und alle 10 s). Ohne Verbindung sind alle Eingaben deaktiviert.
import { writable } from "svelte/store";

export const online = writable(true);
