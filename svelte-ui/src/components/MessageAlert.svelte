<script>
  import { messageStore } from '../stores/messageStore.js';
</script>

{#if $messageStore.show}
  <div id="message" role="alert" class="alert alert-danger">
    <article class={$messageStore.type}>
      {$messageStore.text}
    </article>
  </div>
{/if}

<style>
  /* Immer fest oben am Bildschirm, unabhängig vom Scrollen (überlagert den Inhalt) */
  #message {
    position: fixed;
    top: 0.5rem;
    left: 50%;
    transform: translateX(-50%);
    width: min(calc(100% - 2rem), 34rem);
    z-index: 1000;
    margin: 0;
  }

  #message article {
    margin: 0;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.25);
  }
  
  .error {
    border-left: 4px solid #dc3545;
    background-color: #f8d7da;
    color: #721c24;
  }
  
  .success {
    border-left: 4px solid #28a745;
    background-color: #d4edda;
    color: #155724;
  }
</style>