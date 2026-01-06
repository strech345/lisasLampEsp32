<script>
  // Simple snow animation component
  // We create an array of snowflakes with randomized properties
  const snowflakes = Array.from({ length: 60 }).map((_, i) => ({
    id: i,
    left: Math.random() * 100,
    delay: Math.random() * 5,
    duration: 5 + Math.random() * 10,
    size: 4 + Math.random() * 8, // 4px to 12px
    opacity: 0.3 + Math.random() * 0.5
  }));
</script>

<div class="snow-container" aria-hidden="true">
  {#each snowflakes as flake (flake.id)}
    <div 
      class="snowflake" 
      style="
        left: {flake.left}%; 
        animation-delay: {flake.delay}s; 
        animation-duration: {flake.duration}s;
        width: {flake.size}px;
        height: {flake.size}px;
        opacity: {flake.opacity};
      "
    ></div>
  {/each}
</div>

<style>
  .snow-container {
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
    z-index: 9999;
    overflow: hidden;
  }

  .snowflake {
    position: absolute;
    top: -10px;
    background-color: #e0f2ff; /* Light blue tint */
    border-radius: 50%;
    box-shadow: 0 0 4px rgba(0, 102, 204, 0.4); /* Soft blue shadow for contrast */
    animation: fall linear infinite;
  }

  @keyframes fall {
    0% {
      transform: translateY(0) translateX(0);
    }
    25% {
      transform: translateY(25vh) translateX(15px);
    }
    50% {
      transform: translateY(50vh) translateX(-15px);
    }
    75% {
      transform: translateY(75vh) translateX(15px);
    }
    100% {
      transform: translateY(105vh) translateX(0);
    }
  }
</style>
