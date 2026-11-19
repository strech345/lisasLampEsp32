<script>
  import Main from "./Main.svelte";
  import Settings from "./Settings.svelte";
  
  let page = 'main'; // 'main' | 'settings'
  
  function navigateToSettings() {
    page = 'settings';
  }
  
  function navigateToMain() {
    page = 'main';
  }
</script>

{#if page === 'main'}
  <Main onNavigateToSettings={navigateToSettings} />
{:else if page === 'settings'}
  <Settings onNavigateBack={navigateToMain} />
{/if}