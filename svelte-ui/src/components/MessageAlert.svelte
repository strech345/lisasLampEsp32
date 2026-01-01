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
  #message {
    margin-bottom: 1rem;
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