window.onload = () => {
    let gpx_input = document.getElementById("gpx");
    let titolo = document.getElementById("titolo");
    let fdv = document.getElementById("fdv");
    let fdp = document.getElementById("fdp");
    let start_time = document.getElementById("partenza")
    let submit = document.getElementById("submit");
    let pdf_box = document.getElementById("pdfbox");

    Module.onRuntimeInitialized = () => {
        Module._set_factor(fdv.value);
        Module._set_pause_factor(fdp.value);
    }

    gpx_input.addEventListener("change", () => {
        let reader = new FileReader();
        reader.onload = async (e) => {
            const data = new Uint8Array(e.target.result);

            let source_ptr = Module._get_source_ptr();
            for (let index = 0; index < data.length; index++) {
                Module.HEAPU8[source_ptr+index] = data[index];
            }

            Module._set_source_size(data.length);
        };

        reader.readAsArrayBuffer(gpx_input.files[0]);
    });

    submit.addEventListener("click", async () => {
        let compilation_finished = false;
        let f = (i) => {
            if (compilation_finished) {
                submit.value = "Elabora tabella di marcia";
            } else {
                submit.value = "Attendere" + ".".repeat((i % 3) + 1);
                setTimeout(f, 1000, i+1);
            }
            
        };
        setTimeout(f, 1000, 0);

        Module._set_factor(fdv.value);
        Module._set_pause_factor(fdp.value);
        let [hours, minutes] = start_time.value.split(':');
        let partenza = hours * 60 + minutes;
        Module._set_start_time(partenza);

        if (titolo.value.length > 0) {
            let titolo_str = titolo.value;
            let name_ptr = Module._get_name_ptr();
            let max_name_size = Module._get_name_max_size();

            const textEncoder = new TextEncoder();
            const title_bytes = new Uint8Array(textEncoder.encode(titolo_str));

            for (let i = 0; i < Math.min(max_name_size, titolo_str.length); i++) {
                Module.HEAPU8[name_ptr+i] = title_bytes[i];
            }
            Module.HEAPU8[name_ptr+Math.min(max_name_size, titolo_str.length)] = 0;
        }

        Module._parse_gpx();

        Module._print_latex_document();

        let tex_src_ptr = Module._get_tex_src_ptr();
        let tex_src_size = Module._get_tex_src_size();
        let tex_bytes = Module.HEAPU8.subarray(tex_src_ptr, tex_src_ptr+tex_src_size);

        const textDecoder = new TextDecoder('utf-8');
        const tex_src = textDecoder.decode(tex_bytes);

        // console.log(tex_src);

        const engine = new PdfTeXEngine();
        await engine.loadEngine();
        engine.writeMemFSFile("main.tex", tex_src);
        engine.setEngineMainFile("main.tex");
        let r = await engine.compileLaTeX();

        compilation_finished = true;

        if (r.status === 0) {
            const pdfblob = new Blob([r.pdf], {type : 'application/pdf'});
            const objectURL = URL.createObjectURL(pdfblob);
            setTimeout(() => {
                URL.revokeObjectURL(objectURL);
            }, 30000);
            console.log(objectURL);
            pdf_box.innerHTML = `<embed src="${objectURL}" width="100%" height="400px" type="application/pdf">`;
            // window.open(objectURL, '_blank');
        }
    });
}