import { render } from "preact";
import "../styles/base.css";
import { Styleguide } from "./Styleguide";
import { initTheme } from "../theme";

initTheme();

const root = document.getElementById("app");
if (root) {
  render(<Styleguide />, root);
}
