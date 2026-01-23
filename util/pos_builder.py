import matplotlib.pyplot as plt
import sys

def record_positions(image_path, output_file="../out/positions.txt"):
    # Load and display the image
    try:
        img = plt.imread(image_path)
    except FileNotFoundError:
        print(f"Error: Could not find image at {image_path}")
        return

    fig, ax = plt.subplots()
    ax.imshow(img)
    ax.set_title("Click nodes in order (ID 0, 1, 2...). Close window to save.")

    coords = []

    def onclick(event):
        # Check if click is inside the axes
        if event.xdata is not None and event.ydata is not None:
            # Round to nearest integer
            x = int(round(event.xdata))
            y = int(round(event.ydata))
            
            coords.append((x, y))
            
            # Visual feedback (red dot)
            ax.plot(x, y, 'ro', markersize=5)
            fig.canvas.draw()
            print(f"Captured Node {len(coords)-1}: {x}, {y}")

    # Connect the click event
    cid = fig.canvas.mpl_connect('button_press_event', onclick)
    plt.show()

    # Write to file on close
    with open(output_file, "w") as f:
        for x, y in coords:
            f.write(f"{x},{y}\n")
    print(f"Saved {len(coords)} integer positions to {output_file}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        record_positions(sys.argv[1])
    else:
        print("Usage: python tool.py <path_to_png>")