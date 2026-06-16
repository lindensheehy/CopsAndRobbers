import matplotlib.pyplot as plt
import sys
import os

def record_positions(image_path):
    print(f"Loading image: {image_path}")

    # 1. Load and display the image
    try:
        img = plt.imread(image_path)
    except FileNotFoundError:
        print(f"Error: Could not find image at {image_path}")
        return

    fig, ax = plt.subplots(figsize=(10, 8))
    ax.imshow(img)
    ax.set_title(f"Position Builder: {os.path.basename(image_path)}\nLeft Click to add node. Right Click to undo last. Close to Save.")

    coords = []
    
    # We keep a list of plot objects so we can remove them on undo
    plot_points = [] 

    def onclick(event):
        # Check if click is inside the axes (prevents toolbar clicks crashing it)
        if event.inaxes != ax: return

        if event.button == 1: # Left Click: Add Node
            x = int(round(event.xdata))
            y = int(round(event.ydata))
            
            coords.append((x, y))
            
            # Visual feedback (red dot + number)
            # We store the point and the text so we can remove them if needed
            dot, = ax.plot(x, y, 'ro', markersize=5, zorder=5)
            text = ax.text(x, y, str(len(coords)), color='yellow', fontsize=9, fontweight='bold', ha='right', va='bottom', zorder=6)
            
            plot_points.append((dot, text))
            
            fig.canvas.draw()
            print(f"Node {len(coords)}: ({x}, {y})")
            
        elif event.button == 3: # Right Click: Undo
            if coords:
                coords.pop()
                dot, text = plot_points.pop()
                dot.remove()
                text.remove()
                fig.canvas.draw()
                print("Undid last node.")

    # Connect the click event
    cid = fig.canvas.mpl_connect('button_press_event', onclick)
    plt.show()

    # 2. Generate Output Filename
    # input: "assets/map.png" -> output: "out/map_positions.txt"
    output_dir = "out"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    base_name = os.path.basename(image_path)
    name_no_ext = os.path.splitext(base_name)[0]
    output_file = os.path.join(output_dir, f"{name_no_ext}_positions.txt")

    # 3. Write to file
    if coords:
        with open(output_file, "w") as f:
            for x, y in coords:
                f.write(f"{x},{y}\n")
        print(f"Saved {len(coords)} positions to {output_file}")
    else:
        print("No positions recorded. Output file not created.")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        record_positions(sys.argv[1])
    else:
        print("Usage: python pos_builder.py <path_to_png>")