import { writable } from 'svelte/store';

function createMessageStore() {
  const { subscribe, set } = writable({
    show: false,
    type: 'info',
    text: ''
  });

  let timeoutId = null;

  return {
    subscribe,
    show(type, text) {
      if (timeoutId) {
        clearTimeout(timeoutId);
      }
      
      set({
        show: true,
        type,
        text
      });

      timeoutId = setTimeout(() => {
        set({
          show: false,
          type: 'info',
          text: ''
        });
      }, 5000);
    },
    showPersistent(type, text) {
      if (timeoutId) {
        clearTimeout(timeoutId);
        timeoutId = null;
      }
      
      set({
        show: true,
        type,
        text
      });
    },
    hide() {
      if (timeoutId) {
        clearTimeout(timeoutId);
      }
      set({
        show: false,
        type: 'info',
        text: ''
      });
    }
  };
}

export const messageStore = createMessageStore();