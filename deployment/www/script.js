const localStorageKey = "assetserverdemoimages";
let images = JSON.parse(localStorage.getItem(localStorageKey) ?? "[]");

function fillGallery() {
  if (images.length === 0) {
    gallery.innerHTML = "<p>No images were uploaded yet.</p>";
    return;
  }

  gallery.innerHTML = ""; // Clear previous images
  for (const i of images) {
    const img = document.createElement("img");
    img.src = `/images/${i.hash}/${i.filename}.${i.original.formats[0]}`;
    img.style = `--native-size: ${i.original.width || 1000}px`;
    img.srcset = i.variants
      .map(
        (v) =>
          `/images/${i.hash}/${v.width}x${v.height}/${i.filename}.${v.formats[0]} ${v.width}w`
      )
      .join(", ");
    img.sizes = "24vw";

    gallery.appendChild(img);
  }
}

function resetStatus() {
  statusSpan.innerText = "Waiting for image to upload...";
}

fileInput.addEventListener("change", async () => {
  const f = fileInput.files[0];
  if (!f) return;

  const req = new XMLHttpRequest();
  req.upload.addEventListener("progress", (e) => {
    if (e.loaded == e.total) {
      statusSpan.innerText =
        "Uploaded, waiting for server to process the image";
      return;
    }
    statusSpan.innerText = `Uploading... ${(e.loaded / 1024 / 1024).toFixed(
      1
    )}/${(e.total / 1024 / 1024).toFixed(1)} MB`;
  });
  req.open("POST", "/api/upload?" + new URLSearchParams({ filename: f.name }));
  req.setRequestHeader("Content-Type", f.type);
  req.send(f);

  await new Promise((resolve) => {
    req.addEventListener("load", (e) => {
      resolve();
    });
  });

  const response = JSON.parse(req.responseText);
  if (response.error) {
    statusSpan.innerText = "Error: " + response.error;
    return;
  }

  images.push(response);
  localStorage.setItem(localStorageKey, JSON.stringify(images));
  fillGallery();
  fileInput.value = "";
  statusSpan.innerText = "Image uploaded successfully!";
  setTimeout(resetStatus, 2000);
});

fillGallery();
resetStatus();
