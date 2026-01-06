<script>
  // Birthday Balloon animation component
  const colors = ['#ff595e', '#ffca3a', '#8ac926', '#1982c4', '#6a4c93', '#ff924c'];
  
  const balloons = Array.from({ length: 15 }).map((_, i) => ({
    id: i,
    left: 5 + Math.random() * 90,
    delay: Math.random() * 8,
    duration: 10 + Math.random() * 10,
    size: 40 + Math.random() * 30,
    color: colors[i % colors.length],
    swing: 10 + Math.random() * 20
  }));
</script>

<div class="balloon-container" aria-hidden="true">
  {#each balloons as b (b.id)}
    <div 
      class="balloon-wrapper" 
      style="
        left: {b.left}%; 
        animation-delay: {b.delay}s; 
        animation-duration: {b.duration}s;
        --swing: {b.swing}px;
      "
    >
      <div 
        class="balloon" 
        style="
          background-color: {b.color};
          color: {b.color};
          width: {b.size}px;
          height: {b.size * 1.2}px;
        "
      >
        <div class="string"></div>
      </div>
    </div>
  {/each}
</div>

<style>
  .balloon-container {
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
    z-index: 9998;
    overflow: hidden;
  }

  .balloon-wrapper {
    position: absolute;
    bottom: -150px;
    animation: rise linear infinite;
  }

  .balloon {
    position: relative;
    border-radius: 50% 50% 50% 50% / 40% 40% 60% 60%;
    box-shadow: inset -5px -10px rgba(0,0,0,0.1);
  }

  .balloon::after {
    content: "";
    position: absolute;
    bottom: -5px;
    left: 50%;
    transform: translateX(-50%);
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-bottom: 7px solid inherit;
    border-bottom-color: currentColor; /* Matches balloon color via JS if needed, but here we'll just use a trick */
  }
  
  /* Applying the triangle footer color via a trick to match parent */
  .balloon {
    border-bottom: 0px solid transparent; /* just a placeholder */
  }

  .string {
    position: absolute;
    top: 100%;
    left: 50%;
    width: 1px;
    height: 80px;
    background: rgba(0,0,0,0.2);
  }

  @keyframes rise {
    0% {
      transform: translateY(0) translateX(0);
    }
    25% {
      transform: translateY(-30vh) translateX(var(--swing));
    }
    50% {
      transform: translateY(-60vh) translateX(calc(-1 * var(--swing)));
    }
    75% {
      transform: translateY(-90vh) translateX(var(--swing));
    }
    100% {
      transform: translateY(-120vh) translateX(0);
    }
  }
</style>
